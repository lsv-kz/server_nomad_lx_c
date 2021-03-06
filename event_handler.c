#include "server.h"

#if defined(LINUX_)
    #include <sys/sendfile.h>
#elif defined(FREEBSD_)
    #include <sys/uio.h>
#endif

static Connect *recv_start = NULL;
static Connect *recv_end = NULL;

static Connect *recv_new_start = NULL;
static Connect *recv_new_end = NULL;

static Connect *snd_start = NULL;
static Connect *snd_end = NULL;

static Connect *snd_new_start = NULL;
static Connect *snd_new_end = NULL;

static Connect *pNext = NULL;

static pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
static Connect **arr_conn = NULL;
int num_proc_, ind_;
//======================================================================
int send_part_file(Connect *req, char *buf, int size_buf)
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
            print__err(req, "<%s:%d> Error sendfile(); %s\n", __func__, __LINE__, strerror(errno));
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
                print__err(req, "<%s:%d> Error sendfile()=%d; %s\n", __func__, __LINE__, ret, strerror(errno));
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

        rd = read(req->fd, buf, len);
        if (rd <= 0)
        {
            if (rd == -1)
                print__err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
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
static void del_from_recv_list(Connect *r)
{
    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        recv_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        recv_start = r->next;
    }
    else if (!r->prev && !r->next)
    {
        recv_start = recv_end = NULL;
    }
}
//======================================================================
static void del_from_snd_list(Connect *r)
{
    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        snd_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        snd_start = r->next;
    }
    else if (!r->prev && !r->next)
    {
        snd_start = snd_end = NULL;
    }
}
//======================================================================
static void del_from_list(Connect *r)
{
    if (r->event == POLLOUT)
    {
        if (r == pNext)
            pNext = r->next;

        close(r->fd);
        del_from_snd_list(r);
    }
    else if (r->event == POLLIN)
        del_from_recv_list(r);
    else
        print_err("*** <%s:%d> event=0x%x\n", __func__, __LINE__, r->event);
}
//======================================================================
int set_list(struct pollfd *fdwr)
{
pthread_mutex_lock(&mtx_);
    if (recv_new_start)
    {
        if (recv_end)
            recv_end->next = recv_new_start;
        else
            recv_start = recv_new_start;
        
        recv_new_start->prev = recv_end;
        recv_end = recv_new_end;
        recv_new_start = recv_new_end = NULL;
    }
    
    if (snd_new_start)
    {
        if (snd_end)
            snd_end->next = snd_new_start;
        else
            snd_start = snd_new_start;
        
        snd_new_start->prev = snd_end;
        snd_end = snd_new_end;
        snd_new_start = snd_new_end = NULL;
    }
    
pthread_mutex_unlock(&mtx_);
    int i = 0;
    time_t t = time(NULL);
    //--------------------------- recv ---------------------------------
    Connect *r = recv_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            if (r->reqMethod)
                r->err = -1;
            else
                r->err = NO_PRINT_LOG;
            print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->req_hd.iReferer] = "Timeout";
            
            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;

            fdwr[i].fd = r->clientSocket;
            fdwr[i].events = r->event;
            arr_conn[i] = r;
            ++i;
        }
    }
    ind_ = i;
    //-------------------------- send ----------------------------------
    if (!pNext || !snd_start)
        pNext = snd_start;
    r = pNext;
    next = NULL;
    Connect *r_start = NULL;
    for ( int n_snd = 0; r; r = next)
    {
        next = r->next;
        if (r->first_snd == 1)
        {
            r->first_snd = 0;
            next = NULL;
            break;
        }
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            r->err = -1;
            print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->req_hd.iReferer] = "Timeout";

            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (n_snd == 0)
            {
                r->first_snd = 1;
                r_start = r;
            }
            
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            fdwr[i].fd = r->clientSocket;
            fdwr[i].events = r->event;
            arr_conn[i] = r;
            ++i;
            ++n_snd;
            if (n_snd >= conf->MAX_SND_FD)
                break;
        }

        if (!next)
            next = snd_start;
    }

    pNext = next;

    if (r_start)
        r_start->first_snd = 0;

    return i;
}
//======================================================================
void *event_handler(void *arg)
{
    int num_chld = *((int*)arg);
    int count_resp = 0, all_req = 0;
    int ret = 1, n, wr;
    int size_buf = conf->SEND_FILE_SIZE_PART;
    char *rd_buf = NULL;
    num_proc_ = num_chld;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SEND_FILE != 'y')
#endif
    {
        size_buf = conf->SNDBUF_SIZE;
        rd_buf = malloc(size_buf);
        if (!rd_buf)
        {
            print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            exit(1);
        }
    }

    struct pollfd *fdwr = malloc(sizeof(struct pollfd) * conf->MAX_REQUESTS);
    if (!fdwr)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    arr_conn = malloc(sizeof(Connect*) * conf->MAX_REQUESTS);
    if (!arr_conn)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    while (1)
    {
pthread_mutex_lock(&mtx_);
        
        while ((!recv_start) && (!recv_new_start) && (!snd_start) && (!snd_new_start) && (!close_thr))
        {
            pthread_cond_wait(&cond_, &mtx_);
        }

pthread_mutex_unlock(&mtx_);
        
        if (close_thr)
            break;

        count_resp = set_list(fdwr);
        if (count_resp == 0)
            continue;

        ret = poll(fdwr, count_resp, conf->TIMEOUT_POLL);
        if (ret == -1)
        {
            print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            break;
        }
        else if (ret == 0)
        {
            continue;
        }

        Connect *r = NULL;
        for ( int i = 0; (i < count_resp) && (ret > 0); ++i)
        {
            r = arr_conn[i];
            if (fdwr[i].revents == POLLOUT)
            {
                --ret;
                wr = send_part_file(r, rd_buf, size_buf);
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
            }
            else if (fdwr[i].revents == POLLIN)
            {
                --ret;
                n = hd_read(r);
                if (n < 0)
                {
                    r->err = n;
                    del_from_list(r);
                    end_response(r);
                }
                else if (n > 0)
                {
                    del_from_list(r);
                    push_resp_list(r);
                    all_req++;
                }
                else
                    r->sock_timer = 0;
            }
            else if (fdwr[i].revents)
            {
                --ret;
                print__err(r, "<%s:%d> Error: revents=0x%x\n", __func__, __LINE__, fdwr[i].revents);
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
            }
        }
    }

#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SEND_FILE != 'y')
#endif
        if (rd_buf)
            free(rd_buf);
    free(fdwr);
    free(arr_conn);
    print_err("***** Exit [%s:proc=%d] all req=%d *****\n", __func__, num_chld, all_req);
    return NULL;
}
//======================================================================
void push_pollout_list(Connect *req)
{
    req->event = POLLOUT;
    req->first_snd = 0;
    lseek(req->fd, req->offset, SEEK_SET);
    req->sock_timer = 0;
    req->next = NULL;
pthread_mutex_lock(&mtx_);
    req->prev = snd_new_end;
    if (snd_new_start)
    {
        snd_new_end->next = req;
        snd_new_end = req;
    }
    else
        snd_new_start = snd_new_end = req;
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
    req->prev = recv_new_end;
    if (recv_new_start)
    {
        recv_new_end->next = req;
        recv_new_end = req;
    }
    else
        recv_new_start = recv_new_end = req;
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
    req->bufReq[0] = '\0';
    req->decodeUri[0] = '\0';
    req->sLogTime[0] = '\0';
    req->uri = NULL;
    req->p_newline = req->bufReq;
    req->sReqParam = NULL;
    req->reqHeadersName[0] = NULL;
    req->reqHeadersValue[0] = NULL;
    req->path = NULL;
    req->scriptName = NULL;
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
