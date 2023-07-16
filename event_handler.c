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

static struct pollfd *pollfd_arr;

static pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
static int num_poll, num_work;
static int size_buf;
static char *snd_buf;

static void worker(Connect *r);
//======================================================================
int send_part_file(Connect *req)
{
    int rd, wr, len;
    errno = 0;

    if (req->respContentLength == 0)
        return 0;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile == 'y')
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
                return ERR_TRY_AGAIN;
            print__err(req, "<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    #elif defined(FREEBSD_)
        off_t wr_bytes;
        int ret = sendfile(req->fd, req->clientSocket, req->offset, len, NULL, &wr_bytes, 0);// SF_NODISKIO SF_NOCACHE
        if (ret == -1)
        {
            if (errno == EAGAIN)
            {
                if (wr_bytes == 0)
                    return ERR_TRY_AGAIN;
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
                return ERR_TRY_AGAIN;
            }
            print__err(req, "<%s:%d> Error write(); %s\n", __func__, __LINE__, strerror(errno));
            return -1;
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

    num_work = num_poll = 0;
    time_t t = time(NULL);
    Connect *r = list_start, *next = NULL;

    for ( ; r; r = next)
    {
        next = r->next;

        if (r->sock_timer == 0)
            r->sock_timer = t;

        if (r->io_status == WORK)
        {
            ++num_work;
        }
        else
        {
            if ((t - r->sock_timer) >= r->timeout)
            {
                print__err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
                if (r->operation > READ_REQUEST)
                {
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->req_hd.iReferer] = "Timeout";
                }
                
                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
            else
            {
                pollfd_arr[num_poll].fd = r->clientSocket;
                pollfd_arr[num_poll].events = r->event;
                ++num_poll;
            }
        }
    }

    return num_poll + num_work;
}
//======================================================================
int poll_(int num_chld)
{
    int ret = 0;
    if (num_poll > 0)
    {
        int time_poll = conf->TimeoutPoll;
        if (num_work > 0)
            time_poll = 0;
        
        ret = poll(pollfd_arr, num_poll, time_poll);
        if (ret == -1)
        {
            print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            if (num_work == 0)
                return 0;
        }
    }
    else
    {
        if (num_work == 0)
            return 0;
    }

    int i = 0, all = ret + num_work;
    Connect *r = list_start, *next;
    for ( ; (all > 0) && r; r = next)
    {
        next = r->next;

        if (r->io_status == WORK)
        {
            --all;
            worker(r);
        }
        else
        {
            if ((pollfd_arr[i].revents == POLLOUT) || (pollfd_arr[i].revents == POLLIN))
            {
                --all;
                r->io_status = WORK;
                worker(r);
            }
            else if (pollfd_arr[i].revents)
            {
                --all;
                print__err(r, "<%s:%d> Error: events=0x%x, revents=0x%x\n", __func__, __LINE__, pollfd_arr[i].events, pollfd_arr[i].revents);
                if (r->event == POLLOUT)
                {
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                }

                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
            ++i;
        }
    }

    return 0;
}
//======================================================================
void *event_handler(void *arg)
{
    int num_chld = *((int*)arg);
    int count_resp = 0;
    size_buf = conf->SndBufSize;
    snd_buf = NULL;

#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
    {
        snd_buf = malloc(size_buf);
        if (!snd_buf)
        {
            print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            exit(1);
        }
    }

    pollfd_arr = malloc(sizeof(struct pollfd) * conf->MaxWorkConnections);
    if (!pollfd_arr)
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

        int ret = poll_(num_chld);
        if (ret < 0)
        {
            print_err("[%d]<%s:%d> Error poll_()\n", num_chld, __func__, __LINE__);
            break;
        }
    }

    free(pollfd_arr);
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
        if (snd_buf)
            free(snd_buf);
    return NULL;
}
//======================================================================
void push_send_file(Connect *r)
{
    r->source_entity = FROM_FILE;
    r->operation = SEND_RESP_HEADERS;
    r->io_status = WORK;
    r->event = POLLOUT;
    lseek(r->fd, r->offset, SEEK_SET);
    r->sock_timer = 0;
    r->next = NULL;
pthread_mutex_lock(&mtx_);
    r->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = r;
        list_new_end = r;
    }
    else
        list_new_start = list_new_end = r;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
void push_send_multipart(Connect *r)
{
    r->source_entity = MULTIPART_ENTITY;
    r->operation = SEND_RESP_HEADERS;
    r->io_status = WORK;
    r->event = POLLOUT;
    r->sock_timer = 0;
    r->next = NULL;
pthread_mutex_lock(&mtx_);
    r->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = r;
        list_new_end = r;
    }
    else
        list_new_start = list_new_end = r;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
void push_send_html(Connect *r)
{
    r->source_entity = FROM_DATA_BUFFER;
    r->operation = SEND_RESP_HEADERS;
    r->io_status = WORK;
    r->event = POLLOUT;
    r->sock_timer = 0;
    r->next = NULL;
pthread_mutex_lock(&mtx_);
    r->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = r;
        list_new_end = r;
    }
    else
        list_new_start = list_new_end = r;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
void push_pollin_list(Connect *r)
{
    r->source_entity = NO_ENTITY;
    //r->operation = READ_REQUEST;
    r->io_status = WORK;
    r->event = POLLIN;
    r->sock_timer = 0;
    r->next = NULL;
pthread_mutex_lock(&mtx_);
    r->prev = list_new_end;
    if (list_new_start)
    {
        list_new_end->next = r;
        list_new_end = r;
    }
    else
        list_new_start = list_new_end = r;
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
int set_part(Connect *r)
{
    if (r->indPart >= r->numPart)
    {
        r->mp_status = SEND_END;
        StrClear(&r->msg);
        StrCat(&r->msg, "\r\n--");
        StrCat(&r->msg, boundary);
        StrCat(&r->msg, "--\r\n");
        return 0;
    }

    if (create_multipart_head(r) == 0)
        return -1;
    r->mp_status = SEND_HEADERS;
    r->offset = r->rangeBytes[r->indPart].start;
    r->respContentLength = r->rangeBytes[r->indPart].len;
    r->indPart++;
    lseek(r->fd, r->offset, SEEK_SET);
    return 1;
}
//======================================================================
int send_headers(Connect *r)
{
    int wr = write_to_client(r, r->msg.ptr + r->msg.ind, r->msg.len - r->msg.ind);
    if (wr < 0)
    {
        if (wr == ERR_TRY_AGAIN)
            return ERR_TRY_AGAIN;
        else
            return -1;
    }
    else if (wr > 0)
    {
        r->msg.ind += wr;
    }
    
    return wr;
}
//======================================================================
static void worker(Connect *r)
{
    if (r->operation == SEND_ENTITY)
    {
        if (r->source_entity == FROM_FILE)
        {
            int wr = send_part_file(r);
            if (wr < 0)
            {
                if (wr == ERR_TRY_AGAIN)
                    r->io_status = POLL;
                else
                {
                    r->err = wr;
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
        
                    del_from_list(r);
                    end_response(r);
                }
            }
            else if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr > 0)
                r->sock_timer = 0;
        }
        else if (r->source_entity == MULTIPART_ENTITY)
        {
            if (r->mp_status == SEND_HEADERS)
            {
                int wr = send_headers(r);
                if (wr < 0)
                {
                    if (wr == ERR_TRY_AGAIN)
                        r->io_status = POLL;
                    else
                    {
                        r->err = -1;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr > 0)
                {
                    r->send_bytes += wr;
                    if ((r->msg.len - r->msg.ind) == 0)
                    {
                        r->mp_status = SEND_PART;
                    }
                    r->sock_timer = 0;
                }
            }
            else if (r->mp_status == SEND_PART)
            {
                int wr = send_part_file(r);
                if (wr < 0)
                {
                    if (wr == ERR_TRY_AGAIN)
                        r->io_status = POLL;
                    else
                    {
                        r->err = -1;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr == 0)
                {
                    r->sock_timer = 0;
                    int ret = set_part(r);
                    if (ret == -1)
                    {
                        r->err = -1;
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else
                    r->sock_timer = 0;
            }
            else if (r->mp_status == SEND_END)
            {
                int wr = send_headers(r);
                if (wr < 0)
                {
                    if (wr == ERR_TRY_AGAIN)
                        r->io_status = POLL;
                    else
                    {
                        r->err = -1;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr > 0)
                {
                    r->send_bytes += wr;
                    if ((r->msg.len - r->msg.ind) == 0)
                    {
                        del_from_list(r);
                        end_response(r);
                    }
                    else
                        r->sock_timer = 0;
                }
            }
        }
        else if (r->source_entity == FROM_DATA_BUFFER)
        {
            int ret = send(r->clientSocket, r->html.ptr + r->html.ind, r->respContentLength, 0);
            if (ret == -1)
            {
                if (errno == EAGAIN)
                    r->io_status = POLL;
                else
                {
                    r->err = -1;
                    del_from_list(r);
                    end_response(r);
                }
            }
            else
            {
                r->send_bytes += ret;
                r->html.ind += ret;
                r->respContentLength -= ret;
                if ((r->html.len - r->html.ind) == 0)
                {
                    del_from_list(r);
                    end_response(r);
                }
    
                r->sock_timer = 0;
            }
        }
    }
    else if (r->operation == READ_REQUEST)
    {
        int ret = read_request_headers(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = ret;
                del_from_list(r);
                end_response(r);
            }
        }
        else if (ret > 0)
        {
            del_from_list(r);
            push_resp_list(r);
        }
        else
            r->sock_timer = 0;
    }
    else if (r->operation == SEND_RESP_HEADERS)
    {
        int ret = send(r->clientSocket, r->resp_headers.ptr + r->resp_headers.ind, r->resp_headers.len - r->resp_headers.ind, 0);
        if (ret == -1)
        {
            if (errno == EAGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
        }
        else
        {
            r->sock_timer = 0;
            r->resp_headers.ind += ret;
            if ((r->resp_headers.len - r->resp_headers.ind) == 0)
            {
                if (r->reqMethod == M_HEAD)
                {
                    del_from_list(r);
                    end_response(r);
                }
                else
                {
                    r->operation = SEND_ENTITY;
                    if (r->source_entity == FROM_DATA_BUFFER)
                    {
                        if (r->html.len == 0)
                        {
                            del_from_list(r);
                            end_response(r);
                        }
                    }
                    else if (r->source_entity == FROM_FILE)
                    {
                        ;
                    }
                    else if (r->source_entity == MULTIPART_ENTITY)
                    {
                        if (set_part(r) == -1)
                        {
                            r->err = -1;
                            del_from_list(r);
                            end_response(r);
                        }
                    }
                }
            }
        }
    }
    else
    {
        print__err(r, "<%s:%d> ? operation=%s\n", __func__, __LINE__, get_str_operation(r->operation));
        r->err = -1;
        del_from_list(r);
        end_response(r);
    }
}
