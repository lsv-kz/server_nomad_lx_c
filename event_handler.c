#include "server.h"
#include <sys/sendfile.h>

static Connect *list_start = NULL;
static Connect *list_end = NULL;

static Connect *list_new_start = NULL;
static Connect *list_new_end = NULL;

static pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;

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
int set_list(struct pollfd *fdwr)
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
    int i = 0;
    time_t t = time(NULL);
    Connect *r = list_start, *next;
    
    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            r->err = -1;
            print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
            r->iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->iReferer] = "Timeout";
            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            fdwr[i].fd = r->clientSocket;
            fdwr[i].events = r->event;
            ++i;
        }
    }

    return i;
}
//======================================================================
void *event_handler(void *arg)
{
    int num_chld = *((int*)arg);
    int count_resp = 0;
    int ret = 1, n, wr;
    int size_buf = conf->SNDBUF_SIZE;
    char *rd_buf = NULL;
    
    if (conf->SEND_FILE != 'y')
    {
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

    while (1)
    {
pthread_mutex_lock(&mtx_);
            
            while ((!list_start) && (!list_new_start) && (!close_thr))
            {
                pthread_cond_wait(&cond_, &mtx_);
            }

            if (close_thr)
                break;
pthread_mutex_unlock(&mtx_);
        

        count_resp = set_list(fdwr);
        if (count_resp == 0)
            continue;
        
        ret = poll(fdwr, count_resp, 1);
        if (ret == -1)
        {
            print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            break;
        }
        else if (ret == 0)
        {
            continue;
        }
        
        Connect *r = list_start, *next;
        for (int i = 0; (i < count_resp) && (ret > 0) && r; r = next, ++i)
        {
            next = r->next;

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
                    r->iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->iReferer] = "Connection reset by peer";
                        
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
                    push_req(r);
                }
                else
                    r->sock_timer = 0;
            }
            else if (fdwr[i].revents)
            {
                --ret;
                print__err(r, "<%s:%d> Error: revents=0x%x\n", __func__, __LINE__, fdwr[i].revents);
                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
        }
    }
    
    if (conf->SEND_FILE != 'y')
        free(rd_buf);
    free(fdwr);
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
    req->i_arrHdrs = 0;
    req->lenTail = 0;
    
    req->sizePath = 0;
    req->reqMethod = 0;
    req->uriLen = 0;
    req->lenDecodeUri = 0;
    req->httpProt = 0;
    req->connKeepAlive = 0;

    req->err = 0;
    req->iConnection = -1;
    req->iHost = -1;
    req->iUserAgent = -1;
    req->iReferer = -1;
    req->iUpgrade = -1;
    req->iReqContentType = -1;
    req->iContentLength = -1;
    req->iAcceptEncoding = -1;
    req->iRange = -1;
    req->iIf_Range = -1;

    req->countReqHeaders = 0;
    //--------------------------------
    req->scriptType = 0;
    req->reqContentLength = -1LL;
    req->fileSize = -1LL;

    req->respStatus = 0;
    req->respContentLength = -1LL;

    req->respContentType = NULL;
    
    req->countRespHeaders = 0;
    //------------------------------
    req->send_bytes = 0LL;

    req->numPart = 0;
    req->sRange = NULL;
    req->rangeBytes = NULL;
    
    req->fd = -1;
    req->offset = 0;
}
