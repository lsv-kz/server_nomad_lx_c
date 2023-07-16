#include "server.h"
#include <sys/un.h>

//======================================================================
int create_server_socket(const Config *conf)
{
    int sockfd;
    struct addrinfo  hints, *result, *rp;
    int n;
    const int optval = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((n = getaddrinfo(conf->ServerAddr, conf->ServerPort, &hints, &result)) != 0)
    {
        fprintf(stderr, "Error getaddrinfo(%s:%s): %s\n", conf->ServerAddr, conf->ServerPort, gai_strerror(n));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sockfd);
    }

    if (rp == NULL)
    {
        fprintf(stderr, "Error: failed to bind\n");
        return -1;
    }

    if (conf->TcpNoDelay == 'y')
    {
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&optval, sizeof(optval)); // SOL_TCP
    }

    int flags = fcntl(sockfd, F_GETFL);
    if (flags == -1)
    {
        fprintf(stderr, "Error fcntl(, F_GETFL, ): %s\n", strerror(errno));
    }
    else
    {
        flags |= O_NONBLOCK;
        if (fcntl(sockfd, F_SETFL, flags) == -1)
        {
            fprintf(stderr, "Error fcntl(, F_SETFL, ): %s\n", strerror(errno));
        }
    }

    freeaddrinfo(result);

    if (listen(sockfd, conf->ListenBacklog) == -1)
    {
        fprintf(stderr, "Error listen(): %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}
//======================================================================
int create_fcgi_socket(Connect *r, const char *host)
{
    int sockfd, n;
    char addr[256];
    char port[16];

    if (!host)
        return -1;
    n = sscanf(host, "%[^:]:%s", addr, port);
    if (n == 2) //==== AF_INET ====
    {
        struct sockaddr_in sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1)
        {
            print__err(r, "<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        const int sock_opt = 1;
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))
        {
            print__err(r, "<%s:%d> Error setsockopt(TCP_NODELAY): %s\n", __func__, __LINE__, strerror(errno));
            close(sockfd);
            return -1;
        }

        sock_addr.sin_port = htons(atoi(port));
        sock_addr.sin_family = AF_INET;
        if (inet_aton(addr, &(sock_addr.sin_addr)) == 0)
//      if (inet_pton(AF_INET, addr, &(sock_addr.sin_addr)) < 1)
        {
            print__err(r, "<%s:%d> Error inet_pton(%s): %s\n", __func__, __LINE__, addr, strerror(errno));
            close(sockfd);
            return -1;
        }
        
        int flags = fcntl(sockfd, F_GETFL);
        if (flags == -1)
            print__err(r, "<%s:%d> Error fcntl(, F_GETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        else
        {
            flags |= O_NONBLOCK;
            if (fcntl(sockfd, F_SETFL, flags) == -1)
                print__err(r, "<%s:%d> Error fcntl(, F_SETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        }

        if (connect(sockfd, (struct sockaddr *)(&sock_addr), sizeof(sock_addr)) != 0)
        {
            if (errno != EINPROGRESS)
            {
                print__err(r, "<%s:%d> Error connect(%s): %s\n", __func__, __LINE__, host, strerror(errno));
                close(sockfd);
                return -1;
            }
            else
                r->io_status = POLL;
        }
    }
    else //==== PF_UNIX ====
    {
        struct sockaddr_un sock_addr;
        sockfd = socket (PF_UNIX, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            print__err(r, "<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        sock_addr.sun_family = AF_UNIX;
        snprintf(sock_addr.sun_path, sizeof(sock_addr.sun_path), "%s", host);

        int flags = fcntl(sockfd, F_GETFL);
        if (flags == -1)
        {
            print__err(r, "<%s:%d> Error fcntl(, F_GETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        }
        else
        {
            flags |= O_NONBLOCK;
            if (fcntl(sockfd, F_SETFL, flags) == -1)
            {
                print__err(r, "<%s:%d> Error fcntl(, F_SETFL, ): %s\n", __func__, __LINE__, strerror(errno));
            }
        }

        if (connect(sockfd, (struct sockaddr *) &sock_addr, SUN_LEN(&sock_addr)) == -1)
        {
            if (errno != EINPROGRESS)
            {
                print__err(r, "<%s:%d> Error connect(%s): %s\n", __func__, __LINE__, host, strerror(errno));
                close(sockfd);
                return -1;
            }
            else
                r->io_status = POLL;
        }
    }

    return sockfd;
}
//======================================================================
int get_sock_fcgi(Connect *req, const char *script)
{
    int fcgi_sock = -1, len;
    fcgi_list_addr *ps = conf->fcgi_list;

    if (!script)
    {
        print__err(req, "<%s:%d> Not found\n", __func__, __LINE__);
        return -RS404;
    }

    len = strlen(script);
    if (len > 64)
    {
        print__err(req, "<%s:%d> Error: name script too large\n", __func__, __LINE__);
        return -RS400;
    }

    for (; ps; ps = ps->next)
    {
        if (!strcmp(script, ps->script_name))
            break;
    }

    if (ps != NULL)
    {
        fcgi_sock = create_fcgi_socket(req, ps->addr);
        if (fcgi_sock < 0)
        {
            fcgi_sock = -RS502;
        }
    }
    else
    {
        print__err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, script);
        fcgi_sock = -RS404;
    }

    return fcgi_sock;
}
//======================================================================
int get_sock_buf(int domain, int optname, int type, int protocol)
{
    int sock = socket(domain, type, protocol);
    if (sock < 0)
    {
        fprintf(stderr, "<%s:%d> Error socketpair(): %s\n", __func__, __LINE__, strerror(errno));
        return -errno;
    }

    int sndbuf;
    socklen_t optlen = sizeof(sndbuf);
    if (getsockopt(sock, SOL_SOCKET, optname, &sndbuf, &optlen) < 0)
    {
        fprintf(stderr, "<%s:%d> Error getsockopt(SO_SNDBUF): %s\n", __func__, __LINE__, strerror(errno));
        close(sock);
        return -errno;
    }

    close(sock);
    return sndbuf;
}
//======================================================================
int write_to_client(Connect *req, const char *buf, int len)
{
    int ret = send(req->clientSocket, buf, len, 0);
    if (ret == -1)
    {
        print__err(req, "<%s:%d> Error send(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        else 
            return -1;
    }
    else
        return  ret;
}
//======================================================================
int read_from_client(Connect *req, char *buf, int len)
{
    int ret = recv(req->clientSocket, buf, len, 0);
    if (ret == -1)
    {
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        else
        {
            print__err(req, "<%s:%d> Error recv(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }
    else
        return  ret;
}
//======================================================================
int read_request_headers(Connect *req)
{
    int len = SIZE_BUF_REQUEST - req->lenBufReq - 1;
    if (len <= 0)
        return -RS414;

    int n = recv(req->clientSocket, req->bufReq + req->lenBufReq, len, 0);
    if (n < 0)
    {
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        return -1;
    }
    else if (n == 0)
        return -1;

    req->lenTail += n;

    req->lenBufReq += n;
    req->bufReq[req->lenBufReq] = 0;

    n = find_empty_line(req);
    if (n == 1) // empty line found
        return req->lenBufReq;
    else if (n < 0) // error
        return n;

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
