#include "server.h"
#include <poll.h>
#include <sys/uio.h>
//    #define POLLIN      0x0001    /* Можно считывать данные */
//    #define POLLPRI     0x0002    /* Есть срочные данные */
//    #define POLLOUT     0x0004    /* Запись не будет блокирована */
//    #define POLLERR     0x0008    /* Произошла ошибка */
//    #define POLLHUP     0x0010    /* "Положили трубку" */
//    #define POLLNVAL    0x0020    /* Неверный запрос: fd не открыт */
//======================================================================
int read_timeout(int fd, char *buf, int len, int timeout)
{
    int read_bytes = 0, ret, tm;
    struct pollfd fdrd;
    char *p;

    tm = (timeout == -1) ? -1 : (timeout * 1000);

    fdrd.fd = fd;
    fdrd.events = POLLIN;
    p = buf;
    while (len > 0)
    {
        ret = poll(&fdrd, 1, tm);
        if (ret == -1)
        {
            print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if (!ret)
            return -RS408;

        if (fdrd.revents & POLLIN)
        {
            ret = read(fd, p, len);
            if (ret == -1)
            {
                print_err("<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
            else if (ret == 0)
                break;
            else
            {
                p += ret;
                len -= ret;
                read_bytes += ret;
            }
        }
        else if (fdrd.revents & POLLHUP)
        {
            break;
        }
        else if (fdrd.revents & POLLERR)
        {
            print_err("<%s:%d> POLLERR fdrd.revents = 0x%02x\n", __func__, __LINE__, fdrd.revents);
            return -1;
        }
    }

    return read_bytes;
}
//======================================================================
int write_timeout(int fd, const char *buf, int len, int timeout)
{
    int write_bytes = 0, ret;
    struct pollfd fdwr;

    fdwr.fd = fd;
    fdwr.events = POLLOUT;

    while (len > 0)
    {
        ret = poll(&fdwr, 1, timeout * 1000);
        if (ret == -1)
        {
            print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if (!ret)
        {
            print_err("<%s:%d> TimeOut poll(), tm=%d\n", __func__, __LINE__, timeout);
            return -1;
        }

        if (fdwr.revents != POLLOUT)
        {
            print_err("<%s:%d> 0x%02x\n", __func__, __LINE__, fdwr.revents);
            return -1;
        }

        ret = write(fd, buf, len);
        if (ret == -1)
        {
            print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            if ((errno == EINTR) || (errno == EAGAIN))
                continue;
            return -1;
        }

        write_bytes += ret;
        len -= ret;
        buf += ret;
    }

    return write_bytes;
}
//======================================================================
int client_to_script(int fd_in, int fd_out, long long *cont_len, int num_thr)
{
    int wr_bytes = 0;
    int rd, wr;
    char buf[512];

    for( ; *cont_len > 0; )
    {
        rd = read_timeout(fd_in, buf, (*cont_len > sizeof(buf)) ? sizeof(buf) : *cont_len, conf->TIMEOUT);
        if(rd == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if(rd == 0)
            break;
        *cont_len -= rd;

        wr = write_timeout(fd_out, buf, rd, conf->TIMEOUT_CGI);
        if(wr <= 0)
            return wr;
        wr_bytes += wr;
    }

    return wr_bytes;
}
//======================================================================
long client_to_cosmos(int fd_in, long size)
{
    long wr_bytes = 0;
    long rd;
    char buf[1024];

    for (; size > 0; )
    {
        rd = read_timeout(fd_in, buf, (size > sizeof(buf)) ? sizeof(buf) : size, conf->TIMEOUT);
        if(rd == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if(rd == 0)
            break;

        size -= rd;

        wr_bytes += rd;
    }

    return wr_bytes;
}
//======================================================================
long cgi_to_cosmos(int fd_in, int timeout)
{
    long wr_bytes = 0;
    long rd;
    char buf[1024];

    for (; ; )
    {
        rd = read_timeout(fd_in, buf, sizeof(buf), timeout);
        if(rd == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if(rd == 0)
            break;
        wr_bytes += rd;
    }

    return wr_bytes;
}
//======================================================================
long fcgi_to_cosmos(int fd_in, int size, int timeout)
{
    long wr_bytes = 0;
    long rd;
    char buf[1024];

    for (; size > 0; )
    {
        rd = read_timeout(fd_in, buf, (size > sizeof(buf)) ? sizeof(buf) : size, timeout);
        if(rd == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if(rd == 0)
            break;
        
        size -= rd;
        wr_bytes += rd;
    }

    return wr_bytes;
}
//======================================================================
int fcgi_read_padding(int fd_in, long len, int timeout)
{
    int rd;
    char buf[256];

    for (; len > 0; )
    {
        rd = read_timeout(fd_in, buf, (len > (int)sizeof(buf)) ? (int)sizeof(buf) : len, timeout);
        if (rd == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if (rd == 0)
            return 0;

        len -= rd;
    }

    return 1;
}
//======================================================================
int fcgi_read_stderr(int fd_in, int cont_len, int timeout)
{
    int wr_bytes = 0;
    int rd;
    char buf[512];

    for ( ; cont_len > 0; )
    {
        rd = read_timeout(fd_in, buf, cont_len > (int)sizeof(buf) ? (int)sizeof(buf) : cont_len, conf->TIMEOUT);
        if (rd == -1)
        {
            print_err("<%s:%d> Error read_timeout()=%d\n", __func__, __LINE__, rd);
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if (rd == 0)
            break;

        cont_len -= rd;

        write(STDERR_FILENO, buf, rd);
        wr_bytes += rd;
    }

    write(STDERR_FILENO, "\n", 1);
    return wr_bytes;
}
//======================================================================
int send_file_ux(int fd_out, int fd_in, char *buf, int *size, off_t offset, long long *cont_len)
{
    int rd, wr, ret = 0;

    lseek(fd_in, offset, SEEK_SET);

    for ( ; *cont_len > 0; )
    {
        if(*cont_len < *size)
            rd = read(fd_in, buf, *cont_len);
        else
            rd = read(fd_in, buf, *size);

        if(rd == -1)
        {
            print_err("<%s:%d> Error read(): %s\n", __func__,
                                    __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            ret = rd;
            break;
        }
        else if(rd == 0)
        {
            ret = rd;
            break;
        }

        wr = write_timeout(fd_out, buf, rd, conf->TIMEOUT);
        if(wr <= 0)
        {
            print_err("<%s:%d> Error write_to_sock()=%d\n", __func__, __LINE__, wr);
            ret = -1;
            break;
        }

        *cont_len -= wr;
    }

    return ret;
}
//======================================================================
int hd_read(Connect *req)
{
    int n = recv(req->clientSocket, req->bufReq + req->i_bufReq, LEN_BUF_REQUEST - req->i_bufReq - 1, 0);
    if (n < 0)
    {
        if (errno == EAGAIN)
            return -EAGAIN;
        return -1;
    }
    else if (n == 0)
        return NO_PRINT_LOG;

    req->lenTail += n;

    req->i_bufReq += n;
    req->bufReq[req->i_bufReq] = 0;

    n = empty_line(req);
    if (n == 1)
    {
        return req->i_bufReq;
    }
    else if (n < 0)
        return n;

    return 0;
}
//======================================================================
int empty_line(Connect *req)
{
    req->timeout = conf->TIMEOUT;
    char *pr, *pn, ch;
    while (req->lenTail > 0)
    {
        int i = 0, len_line = 0;
        pr = pn = NULL;
        while (i < req->lenTail)
        {
            ch = *(req->p_newline + i);
            if (ch == '\r')
            {
                if (i == (req->lenTail - 1))
                    return 0;
                pr = req->p_newline + i;
            }
            else if (ch == '\n')
            {
                pn = req->p_newline + i;
                if ((pr) && ((pn - pr) != 1))
                    return -RS400;
                i++;
                break;
            }
            else
                len_line++;
            i++;
        }

        if (pn)
        {
            if (pr == NULL)
                *pn = 0;
            else
                *pr = 0;

            if (len_line == 0)
            {
                if (req->countReqHeaders == 0)
                {
                    if ((pn - req->bufReq + 1) > 4)
                        return -RS400;
                    req->lenTail -= i;
                    req->p_newline = pn + 1;
                    continue;
                }

                if (req->lenTail > 0)
                {
                    req->tail = pn + 1;
                    req->lenTail -= i;
                }
                else 
                    req->tail = NULL;
                return 1;
            }

            if (req->countReqHeaders < MAX_HEADERS)
            {
                req->reqHeadersName[req->countReqHeaders] = req->p_newline;
                req->countReqHeaders++;
            }
            else
                return -RS500;

            req->lenTail -= i;
            req->p_newline = pn + 1;
        }
        else if (pr && (!pn))
            return -RS400;
        else
            break;
    }

    return 0;
}
//======================================================================
int send_fd(int unix_sock, int fd, void *data, int size_data)
{
    struct msghdr msgh;
    struct iovec iov;
    ssize_t ret;
    char buf[CMSG_SPACE(sizeof(int))];
    memset(buf, 0, sizeof(buf));

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    iov.iov_base = data;
    iov.iov_len = size_data;

    if (fd != -1)
    {
        msgh.msg_control = buf;
        msgh.msg_controllen = sizeof(buf);

        struct cmsghdr *cmsgp = CMSG_FIRSTHDR(&msgh);
        cmsgp->cmsg_len = CMSG_LEN(sizeof(int));
        cmsgp->cmsg_level = SOL_SOCKET;
        cmsgp->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsgp), &fd, sizeof(int));
    }
    else
    {
        msgh.msg_control = NULL;
        msgh.msg_controllen = 0;
    }

    ret = sendmsg(unix_sock, &msgh, 0);
    if (ret == -1)
    {
        if (errno == ENOBUFS)
            ret = -ENOBUFS;
        else
            print_err("<%s:%d> Error sendmsg(): %s\n", __func__, __LINE__, strerror(errno));
    }

    return ret;
}
//======================================================================
int recv_fd(int unix_sock, int num_chld, void *data, int *size_data)
{
    int fd;
    struct msghdr msgh;
    struct iovec iov;
    ssize_t ret;
    char buf[CMSG_SPACE(sizeof(int))];

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    iov.iov_base = data;
    iov.iov_len = *size_data;

    msgh.msg_control = buf;
    msgh.msg_controllen = sizeof(buf);

    ret = recvmsg(unix_sock, &msgh, 0);
    if (ret <= 0)
    {
        if (ret < 0)
            print_err("[%d]<%s:%d> Error recvmsg(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        return -1;
    }

    *size_data = ret;

    struct cmsghdr *cmsgp = CMSG_FIRSTHDR(&msgh);
    if (cmsgp == NULL || cmsgp->cmsg_len != CMSG_LEN(sizeof(int)))
    {
        print_err("[%d]<%s:%d> bad cmsg header\n", num_chld, __func__, __LINE__);
        return -1;
    }
    if (cmsgp->cmsg_level != SOL_SOCKET)
    {
        print_err("[%d]<%s:%d> cmsg_level != SOL_SOCKET\n", num_chld, __func__, __LINE__);
        return -1;
    }
    if (cmsgp->cmsg_type != SCM_RIGHTS)
    {
        print_err("[%d]<%s:%d> cmsg_type != SCM_RIGHTS\n", num_chld, __func__, __LINE__);
        return -1;
    }

    memcpy(&fd, CMSG_DATA(cmsgp), sizeof(int));
    return fd;
}
