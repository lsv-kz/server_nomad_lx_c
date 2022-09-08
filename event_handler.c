#include "server.h"

#if defined(LINUX_)
    #include <sys/sendfile.h>
#elif defined(FREEBSD_)
    #include <sys/uio.h>
#endif

static Connect *list_start = NULL;
static Connect *list_end = NULL;

static Connect *list_new_start = NULL;
static Connect *list_new_end = NULL;

static Connect **conn_array;
static struct pollfd *pollfd_arr;

static pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
static int size_buf;
static char *snd_buf;
//======================================================================
int send_part_file(Connect *req)
{
    int rd, wr, len;
    errno = 0;

    if (req->respContentLength == 0)
        return 0;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SEND_FILE == 'y')
    {
        if (req->respContentLength >= size_buf)
            len = size_buf;
        else
            len = req->respContentLength;
    #if defined(LINUX_)
        wr = sendfile(req->clientSocket, req->fd, &req->offset, len);
        if (wr == -1)
        {
            if (errno == EAGAIN)
                return -EAGAIN;
            print__err(req, "<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
    #elif defined(FREEBSD_)
        off_t wr_bytes;
        int ret = sendfile(req->fd, req->clientSocket, req->offset, len, NULL, &wr_bytes, 0);// SF_NODISKIO SF_NOCACHE
        if (ret == -1)
        {
            if (errno == EAGAIN)
            {
                if (wr_bytes == 0)
                    return -EAGAIN;
                req->offset += wr_bytes;
                wr = wr_bytes;
            }
            else
            {
                print__err(req, "<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
        }
        else if (ret == 0)
        {
            req->offset += wr_bytes;
            wr = wr_bytes;
        }
        else
        {
            print__err(req, "<%s:%d> Error sendfile()=%d, wr_bytes=%ld\n", __func__, __LINE__, ret, wr_bytes);
            return -1;
        }
    #endif
    }
    else
#endif
    {
        if (req->respContentLength >= size_buf)
            len = size_buf;
        else
            len = req->respContentLength;

        rd = read(req->fd, snd_buf, len);
        if (rd <= 0)
        {
            if (rd == -1)
                print__err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return rd;
        }

        wr = write(req->clientSocket, snd_buf, rd);
        if (wr == -1)
        {
            if (errno == EAGAIN)
            {
                lseek(req->fd, -rd, SEEK_CUR);
                return -EAGAIN;
            }
            print__err(req, "<%s:%d> Error write(); %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
        else if (rd != wr)
            lseek(req->fd, wr - rd, SEEK_CUR);
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
    if (r->event == POLLOUT)
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
}
//======================================================================
int set_list()
{
pthread_mutex_lock(&mtx_);
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
pthread_mutex_unlock(&mtx_);

    time_t t = time(NULL);
    Connect *r = list_start, *next = NULL;
    int i = 0;

    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            if (r->reqMethod)
            {
                r->err = -1;
                print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHeadersValue[r->req_hd.iReferer] = "Timeout";
            }
            else
                r->err = NO_PRINT_LOG;

            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            pollfd_arr[i].fd = r->clientSocket;
            pollfd_arr[i].events = r->event;
            conn_array[i] = r;
            ++i;
        }
    }

    return i;
}
//======================================================================
int poll_(int num_chld, int i, int nfd)
{
    int ret = poll(pollfd_arr + i, nfd, conf->TIMEOUT_POLL);
    if (ret == -1)
    {
        print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (ret == 0)
        return 0;

    Connect *r = NULL;
    for ( ; (i < nfd) && (ret > 0); ++i)
    {
        r = conn_array[i];
        if (pollfd_arr[i].revents == POLLOUT)
        {
            int wr = send_part_file(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr == -1)
            {
                r->err = wr;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";

                del_from_list(r);
                end_response(r);
            }
            else if (wr > 0) 
                r->sock_timer = 0;
            else if (wr == -EAGAIN)
            {
                r->sock_timer = 0;
                print__err(r, "<%s:%d> Error: EAGAIN\n", __func__, __LINE__);
            }
            --ret;
        }
        else if (pollfd_arr[i].revents == POLLIN)
        {
            int n = hd_read(r);
            if (n == -EAGAIN)
                r->sock_timer = 0;
            else if (n < 0)
            {
                r->err = n;
                del_from_list(r);
                end_response(r);
            }
            else if (n > 0)
            {
                del_from_list(r);
                push_resp_list(r);
            }
            else
                r->sock_timer = 0;
            --ret;
        }
        else if (pollfd_arr[i].revents)
        {
            print__err(r, "<%s:%d> Error: events=0x%x, revents=0x%x\n", __func__, __LINE__, pollfd_arr[i].events, pollfd_arr[i].revents);
            if (r->event == POLLOUT)
            {
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                r->err = -1;
            }
            else
                r->err = NO_PRINT_LOG;

            del_from_list(r);
            end_response(r);
            --ret;
        }
    }

    return i;
}
//======================================================================
void *event_handler(void *arg)
{
    int num_chld = *((int*)arg);
    int count_resp = 0;
    size_buf = conf->SNDBUF_SIZE;
    snd_buf = NULL;

#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SEND_FILE != 'y')
#endif
    {
        snd_buf = malloc(size_buf);
        if (!snd_buf)
        {
            print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            exit(1);
        }
    }

    pollfd_arr = malloc(sizeof(struct pollfd) * conf->MAX_WORK_CONNECT);
    if (!pollfd_arr)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    conn_array = malloc(sizeof(Connect*) * conf->MAX_WORK_CONNECT);
    if (!conn_array)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    while (1)
    {
pthread_mutex_lock(&mtx_);
        while ((!list_start) && (!list_new_start) && (!close_thr))
        {
            pthread_cond_wait(&cond_, &mtx_);
        }
pthread_mutex_unlock(&mtx_);

        if (close_thr)
            break;

        count_resp = set_list();
        if (count_resp == 0)
            continue;

        int nfd;
        for (int i = 0; count_resp > 0; )
        {
            if (count_resp > conf->MAX_EVENT_CONNECT)
                nfd = conf->MAX_EVENT_CONNECT;
            else
                nfd = count_resp;

            int ret = poll_(num_chld, i, nfd);
            if (ret < 0)
            {
                print_err("[%d]<%s:%d> Error poll_()\n", num_chld, __func__, __LINE__);
                break;
            }
            else if (ret == 0)
                break;

            i += nfd;
            count_resp -= nfd;
        }
    }

    free(pollfd_arr);
    free(conn_array);
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SEND_FILE != 'y')
#endif
        if (snd_buf)
            free(snd_buf);
    print_err("***** Exit [%s:proc=%d] *****\n", __func__, num_chld);
    return NULL;
}
//======================================================================
void push_pollout_list(Connect *req)
{
    req->event = POLLOUT;
    lseek(req->fd, req->offset, SEEK_SET);
    req->sock_timer = 0;
    req->next = NULL;
pthread_mutex_lock(&mtx_);
    req->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = req;
        list_new_end = req;
    }
    else
        list_new_start = list_new_end = req;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
void push_pollin_list(Connect *req)
{
    init_struct_request(req);
    get_time(req->sLogTime, sizeof(req->sLogTime));
    req->event = POLLIN;
    req->sock_timer = 0;
    req->next = NULL;
pthread_mutex_lock(&mtx_);
    req->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = req;
        list_new_end = req;
    }
    else
        list_new_start = list_new_end = req;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
void close_event_handler(void)
{
    close_thr = 1;
    pthread_cond_signal(&cond_);
}
//======================================================================
void init_struct_request(Connect *req)
{
    //str_free(&req->path);
    //str_free(&req->scriptName);
    
    req->bufReq[0] = '\0';
    req->decodeUri[0] = '\0';
    req->sLogTime[0] = '\0';
    req->uri = NULL;
    req->p_newline = req->bufReq;
    req->sReqParam = NULL;
    req->reqHeadersName[0] = NULL;
    req->reqHeadersValue[0] = NULL;
    req->i_bufReq = 0;
    req->lenTail = 0;
    req->sizePath = 0;
    req->reqMethod = 0;
    req->uriLen = 0;
    req->lenDecodeUri = 0;
    req->httpProt = 0;
    req->connKeepAlive = 0;
    req->err = 0;
    req->req_hd = (ReqHd){-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1LL};
    req->countReqHeaders = 0;
    req->scriptType = 0;
    req->fileSize = -1LL;
    req->respStatus = 0;
    req->respContentLength = -1LL;
    req->respContentType = NULL;
    req->countRespHeaders = 0;
    req->send_bytes = 0LL;
    req->numPart = 0;
    req->sRange = NULL;
    req->rangeBytes = NULL;
    req->fd = -1;
    req->offset = 0;
}
