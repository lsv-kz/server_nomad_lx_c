#include "server.h"
#include <sys/sendfile.h>

static Connect *list_start = NULL;
static Connect *list_end = NULL;

static Connect *list_new_start = NULL;
static Connect *list_new_end = NULL;

static pthread_mutex_t mtx_send = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_add = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
//======================================================================
int send_part_file(Connect *req, char *buf, int size_buf)
{
    int rd, wr, len;
    errno = 0;
    
    if (req->respContentLength == 0)
        return 0;
    
    if (req->respContentLength >= size_buf)
        len = size_buf;
    else
        len = req->respContentLength;
    
    if (conf->SEND_FILE == 'y')
    {
        wr = sendfile(req->clientSocket, req->fd, &req->offset, len);
        if (wr == -1)
        {
            if (errno == EAGAIN)
                return -EAGAIN;
            print_err("<%s:%d> Error sendfile(); %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
    }
    else
    {
        rd = read(req->fd, buf, len);
        if (rd <= 0)
        {
            if (rd == -1)
                print_err("<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return rd;
        }

        wr = write(req->clientSocket, buf, rd);
        if (wr == -1)
        {
            if (errno == EAGAIN)
            {
                lseek(req->fd, -rd, SEEK_CUR);
                return -EAGAIN;
            }
            print_err("<%s:%d> Error write(); %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
        else if (rd != wr)
        {
            lseek(req->fd, wr - rd, SEEK_CUR);
        }
    }
    
    req->send_bytes += wr;
    req->respContentLength -= wr;
    if (req->respContentLength == 0)
        wr = 0;

    return wr;
}
//======================================================================
static void del_from_list(Connect *r)
{
    close(r->fd);
    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        list_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        list_start = r->next;
    }
    else if (!r->prev && !r->next)
    {
        list_start = list_end = NULL;
    }
    end_response(r);
}
//======================================================================
int set_list(struct pollfd *fdwr)
{
pthread_mutex_lock(&mtx_send);
    if (list_new_start)
    {
        if (list_end)
            list_end->next = list_new_start;
        else
            list_start = list_new_start;
        
        list_new_start->prev = list_end;
        list_end = list_new_end;
        list_new_start = list_new_end = NULL;
    }
pthread_mutex_unlock(&mtx_send);
    int i = 0;
    time_t t = time(NULL);
    Connect *r = list_start, *next;
    
    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= conf->TimeOut) && (r->sock_timer != 0))
        {
            r->err = -1;
            print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
            r->iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->iReferer] = "Timeout";
            del_from_list(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            fdwr[i].fd = r->clientSocket;
            fdwr[i].events = POLLOUT;
            ++i;
        }
    }

    return i;
}
//======================================================================
void *send_files(void *arg)
{
    int count_resp = 0;
    int i, ret = 0;
    int size_buf = conf->SOCK_BUFSIZE;
    char *rd_buf = NULL;
    
    if (conf->SEND_FILE != 'y')
    {
        rd_buf = malloc(size_buf);
        if (!rd_buf)
        {
            print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            exit(1);
        }
    }
    
    struct pollfd *fdwr = malloc(sizeof(struct pollfd) * conf->MAX_REQUESTS);
    if (!fdwr)
    {
        print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(1);
    }
    
    i = 0;
    while (1)
    {
    pthread_mutex_lock(&mtx_send);
        while ((list_start == NULL) && (list_new_start == NULL) && (!close_thr))
        {
            pthread_cond_wait(&cond_add, &mtx_send);
        }
    pthread_mutex_unlock(&mtx_send);
        if (close_thr)
            break;
        
        count_resp = set_list(fdwr);
        if (count_resp == 0)
        {
            print_err("<%s:%d> count_resp=%d\n", __func__, __LINE__, count_resp);
            continue;
        }

        ret = poll(fdwr, count_resp, 100);
        if (ret == -1)
        {
            print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            break;
        }
        else if (ret == 0)
        {
            continue;
        }

        if (count_resp < 100) size_buf = conf->SOCK_BUFSIZE;
        Connect *r = list_start, *next;
        i = 0;
        for (; (i < count_resp) && (ret > 0) && r; r = next, ++i)
        {
            next = r->next;
            if (fdwr[i].revents == POLLOUT)
            {
                --ret;
                int wr = send_part_file(r, rd_buf, size_buf);
                if (wr == 0)
                {
                    del_from_list(r);
                }
                else if (wr == -1)
                {
                    r->err = -1;
                    r->iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->iReferer] = "Connection reset by peer";
                    del_from_list(r);
                }
                else if (wr > 0) 
                {
                    r->sock_timer = 0;
                }
                else if (wr == -EAGAIN)
                {
                    if (size_buf > 8192) size_buf = size_buf/2;
                    r->sock_timer = 0;
                }
            }
            else if (fdwr[i].revents != 0)
            {
                --ret;
                r->err = -1;
                print_err("<%s:%d> revents=0x%x\n", __func__, __LINE__, fdwr[i].revents);
                del_from_list(r);
            }
        }
    }
    
    if (conf->SEND_FILE != 'y')
        free(rd_buf);
    free(fdwr);

    return NULL;
}
//======================================================================
void push_resp_queue(Connect *req)
{
    lseek(req->fd, req->offset, SEEK_SET);
    free_range(req);
    req->sock_timer = 0;
    req->next = NULL;
pthread_mutex_lock(&mtx_send);
    req->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = req;
        list_new_end = req;
    }
    else
        list_new_start = list_new_end = req;
pthread_mutex_unlock(&mtx_send);
    pthread_cond_signal(&cond_add);
}
//======================================================================
void close_queue(void)
{
    close_thr = 1;
    pthread_cond_signal(&cond_add);
}
