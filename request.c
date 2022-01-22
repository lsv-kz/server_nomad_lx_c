#include "server.h"

static Connect *list_start = NULL;
static Connect *list_end = NULL;

static Connect *list_new_start = NULL;
static Connect *list_new_end = NULL;

pthread_mutex_t mtx_req = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_add = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
struct pollfd *fdrd;
//======================================================================
static void del_from_list(Connect *r)
{
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
void close_conn(Connect *r)
{
    r->err = -1;
    del_from_list(r);
    end_response(r);
}
//======================================================================
void push_list2(Connect *r)
{
    del_from_list(r);
    push_req(r);
}
//======================================================================
static int set_list(struct pollfd *fdwr)
{
pthread_mutex_lock(&mtx_req);
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
pthread_mutex_unlock(&mtx_req);
    
    int i = 0;
    time_t t = time(NULL);
    Connect *r = list_start, *next;
    
    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
            close_conn(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            fdrd[i].fd = r->clientSocket;
            fdrd[i].events = POLLIN;
            ++i;
        }
    }

    return i;
}
//======================================================================
void *get_request(void *arg)
{
    int count_resp = 0;
    int ret = 0;
    int timeout = 5;
    
    fdrd = malloc(sizeof(struct pollfd) * conf->MAX_REQUESTS);
    if (!fdrd)
    {
        print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(1);
    }

    while (1)
    {
    pthread_mutex_lock(&mtx_req);
        while ((list_start == NULL) && (list_new_start == NULL) && (!close_thr))
        {
            pthread_cond_wait(&cond_add, &mtx_req);
        }

        if (close_thr)
            break;
    pthread_mutex_unlock(&mtx_req);

        count_resp = set_list(fdrd);
        if (count_resp == 0)
        {
//            print_err("<%s:%d> list_start=%p, list_new_start=%p\n", __func__, __LINE__, list_start, list_new_start);
            continue;
        }
        
        ret = poll(fdrd, count_resp, timeout); 
        if (ret == -1)
        {
            print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
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

            if (fdrd[i].revents == POLLIN)
            {
                --ret;
                int n = hd_read(r);
                if (n < 0)
                {
                    close_conn(r);
                }
                else if (n > 0)
                    push_list2(r);
            }
            else if (fdrd[i].revents)
            {
                --ret;
                print_err("<%s:%d> Error: fdwr.revents != 0\n", __func__, __LINE__);
                close_conn(r);
            }
        }
    }
    
//    print_err("<%s:%d> *** Exit queue2() ***\n", __func__, __LINE__);
    free(fdrd);
    return NULL;
}
//======================================================================
/*void push_list1(Connect *req)
{
    init_struct_request(req);
    req->sock_timer = 0;
    req->next = NULL;
pthread_mutex_lock(&mtx_req);
    req->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = req;
        list_new_end = req;
    }
    else
        list_new_start = list_new_end = req;

pthread_mutex_unlock(&mtx_req);
    pthread_cond_signal(&cond_add);
}
//======================================================================
void close_request(void)
{
    close_thr = 1;
    pthread_cond_signal(&cond_add);
}*/
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
    /*------------------------------*/
    req->scriptType = 0;
    req->reqContentLength = -1LL;
    req->fileSize = -1LL;

    req->respStatus = 0;
    req->respContentLength = -1LL;

    req->respContentType = NULL;
    
    req->countRespHeaders = 0;
    /*----------------------------*/
    req->send_bytes = 0LL;

    req->numPart = 0;
    req->sRange = NULL;
    req->rangeBytes = NULL;
    
    req->fd = -1;
    req->offset = 0;
}
