#include "server.h"
//======================================================================
static Connect *work_list_start = NULL;
static Connect *work_list_end = NULL;

static Connect *wait_list_start = NULL;
static Connect *wait_list_end = NULL;

struct pollfd *cgi_poll_fd;

static pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;

static int close_thr = 0;
static unsigned int num_wait, num_work;

static int n_work, n_poll;
//======================================================================
int cgi_set_size_chunk(Connect *req);
static void cgi_set_poll_list(Connect *r, int*);
static void cgi_worker(Connect* r);
void cgi_set_status_readheaders(Connect *r);
int timeout_cgi(Connect *r);
int cgi_create_pipes(Connect *req);

void fcgi_set_poll_list(Connect *r, int *i);
void fcgi_worker(Connect* r);
int timeout_fcgi(Connect *r);
int fcgi_create_connect(Connect *req);

void scgi_worker(Connect* r);
int timeout_scgi(Connect *r);
int scgi_create_connect(Connect *req);
//======================================================================
const char *get_script_name(const char *name)
{
    const char *p;
    if (!name)
        return "";
    if ((p = strchr(name + 1, '/')))
        return p;
    return name;
}
//======================================================================
void wait_pid(Connect *r)
{
    int n = waitpid(r->cgi.pid, NULL, WNOHANG); // no blocking
    if (n == -1)
    {
        //print__err(r, "<%s:%d> Error waitpid(%d): %s\n", __func__, __LINE__, r->cgi.pid, strerror(errno));
    }
    else if (n == 0)
    {
        if (kill(r->cgi.pid, SIGKILL) == 0)
            waitpid(r->cgi.pid, NULL, 0);
        else
            print__err(r, "<%s:%d> Error kill(%d): %s\n", __func__, __LINE__, r->cgi.pid, strerror(errno));
    }
}
//======================================================================
void cgi_del_from_list(Connect *r)
{
    if ((r->cgi_type == CGI) || 
        (r->cgi_type == PHPCGI))
    {
        if (r->cgi.from_script > 0)
        {
            close(r->cgi.from_script);
            r->cgi.from_script = -1;
        }

        if (r->cgi.to_script > 0)
        {
            close(r->cgi.to_script);
            r->cgi.to_script = -1;
        }
        
        wait_pid(r);
    }
    else if ((r->cgi_type == PHPFPM) || 
            (r->cgi_type == FASTCGI) ||
            (r->cgi_type == SCGI))
    {
        if (r->fcgi.fd > 0)
        {
            shutdown(r->fcgi.fd, SHUT_RDWR);
            close(r->fcgi.fd);
        }
    }

    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        work_list_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        work_list_start = r->next;
    }
    else if (!r->prev && !r->next)
        work_list_start = work_list_end = NULL;
pthread_mutex_lock(&mtx_);
    --num_work;
pthread_mutex_unlock(&mtx_);
}
//======================================================================
static void cgi_add_work_list()
{
pthread_mutex_lock(&mtx_);
    if ((num_work < conf->MaxCgiProc) && wait_list_end)
    {
        int n_max = conf->MaxCgiProc - num_work;
        Connect *r = wait_list_end;

        for ( ; (n_max > 0) && r; r = wait_list_end, --n_max)
        {
            wait_list_end = r->prev;
            if (wait_list_end == NULL)
                wait_list_start = NULL;
            --num_wait;
            //--------------------------
            if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
            {
                int ret = cgi_create_pipes(r);
                if (ret < 0)
                {
                    print__err(r, "<%s:%d> Error cgi_create_pipes(): %d\n", __func__, __LINE__, ret);
                    r->err = ret;
                    end_response(r);
                    continue;
                }
            }
            else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
            {
                r->fcgi.size_par = r->fcgi.i_param = 0;
                int ret = fcgi_create_connect(r);
                if (ret < 0)
                {
                    r->err = ret;
                    end_response(r);
                    continue;
                }

                r->fcgi.status = FCGI_READ_DATA;
                r->fcgi.len_buf = 0;
            }
            else if (r->cgi_type == SCGI)
            {
                r->fcgi.size_par = r->fcgi.i_param = 0;
                int ret = scgi_create_connect(r);
                if (ret < 0)
                {
                    r->err = ret;
                    end_response(r);
                    continue;
                }
            }
            else
            {
                print__err(r, "<%s:%d> operation=%d, cgi_type=%ld\n", __func__, __LINE__, r->operation, r->cgi_type);
                end_response(r);
                continue;
            }
            //--------------------------
            if (work_list_end)
                work_list_end->next = r;
            else
                work_list_start = r;

            r->prev = work_list_end;
            r->next = NULL;
            work_list_end = r;
            ++num_work;
        }
    }
pthread_mutex_unlock(&mtx_);
}
//======================================================================
static void set_poll_list()
{
    time_t t = time(NULL);
    n_work = n_poll = 0;

    Connect *r = work_list_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;

        if (r->sock_timer == 0)
            r->sock_timer = t;

        if (r->io_status == WORK)
        {
            ++n_work;
            return;
        }

        if ((t - r->sock_timer) >= r->timeout)
        {
            if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
                r->err = timeout_cgi(r);
            else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
                r->err = timeout_fcgi(r);
            else if (r->cgi_type == SCGI)
                r->err = timeout_scgi(r);
            else
            {
                print__err(r, "<%s:%d> cgi_type=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
                r->err = -1;
            }
    
            print__err(r, "<%s:%d> Timeout=%ld, err=%d\n", __func__, __LINE__, t - r->sock_timer, r->err);
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->req_hd.iReferer] = "Timeout";
            
            cgi_del_from_list(r);
            end_response(r);
        }
        else
        {
            switch (r->cgi_type)
            {
                case CGI:
                case PHPCGI:
                    cgi_set_poll_list(r, &n_poll);
                    break;
                case PHPFPM:
                case FASTCGI:
                case SCGI:
                    fcgi_set_poll_list(r, &n_poll);
                    break;
                default:
                    print__err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
                    r->err = -RS500;
                    cgi_del_from_list(r);
                    end_response(r);
                    break;
            }
        }
    }
}
//======================================================================
static int poll_(int num_chld)
{
    int ret = 0;
    if (n_poll > 0)
    {
        int time_poll = conf->TimeoutPoll;
        if (n_work > 0)
            time_poll = 0;

        ret = poll(cgi_poll_fd, n_poll, time_poll);
        if (ret == -1)
        {
            print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            if (n_work == 0)
                return 0;
        }
    }
    else
    {
        if (n_work == 0)
            return 0;
    }

    int i = 0, all = ret + n_work;
    Connect *r = work_list_start, *next = NULL;
    for ( ; (all > 0) && r; r = next)
    {
        next = r->next;
        if (r->io_status == WORK)
        {
            --all;
            if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
                cgi_worker(r);
            else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
                fcgi_worker(r);
            else if (r->cgi_type == SCGI)
                scgi_worker(r);
        }
        else
        {
            if ((cgi_poll_fd[i].revents == POLLOUT) || (cgi_poll_fd[i].revents & POLLIN))
            {
                --all;
                r->io_status = WORK;
                if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
                    cgi_worker(r);
                else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
                    fcgi_worker(r);
                else if (r->cgi_type == SCGI)
                    scgi_worker(r);
            }
            else if (cgi_poll_fd[i].revents)
            {
                --all;
                if (cgi_poll_fd[i].fd == r->clientSocket)
                {
                    print__err(r, "<%s:%d> Error: fd=%d, events=0x%x(0x%x), send_bytes=%lld\n", 
                            __func__, __LINE__, r->clientSocket, cgi_poll_fd[i].events, cgi_poll_fd[i].revents, r->send_bytes);
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                    r->err = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                }
                else
                {
                    switch (r->cgi_type)
                    {
                        case CGI:
                        case PHPCGI:
                            if ((r->cgi.op.cgi == CGI_SEND_ENTITY) && (r->cgi.dir == FROM_CGI))
                            {
                                if (r->mode_send == CHUNK)
                                {
                                    r->cgi.len_buf = 0;
                                    r->cgi.p = r->cgi_buf + 8;
                                    cgi_set_size_chunk(r);
                                    r->cgi.dir = TO_CLIENT;
                                    r->mode_send = CHUNK_END;
                                    r->sock_timer = 0;
                                }
                                else
                                {
                                    cgi_del_from_list(r);
                                    end_response(r);
                                }
                            }
                            else
                            {
                                print__err(r, "<%s:%d> Error: events=0x%x(0x%x), %s/%s\n", __func__, __LINE__, 
                                       cgi_poll_fd[i].events, cgi_poll_fd[i].revents, get_cgi_operation(r->cgi.op.cgi), get_cgi_dir(r->cgi.dir));
                                if (r->cgi.op.cgi <= CGI_READ_HTTP_HEADERS)
                                    r->err = -RS502;
                                else
                                    r->err = -1;
                                cgi_del_from_list(r);
                                end_response(r);
                            }
                            break;
                        case PHPFPM:
                        case FASTCGI:
                            print__err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", 
                                        __func__, __LINE__, cgi_poll_fd[i].events, cgi_poll_fd[i].revents);
                            if (r->cgi.op.fcgi <= FASTCGI_READ_HTTP_HEADERS)
                                r->err = -RS502;
                            else
                                r->err = -1;
                            cgi_del_from_list(r);
                            end_response(r);
                            break;
                        case SCGI:
                            if ((r->cgi.op.scgi == SCGI_SEND_ENTITY) && (r->cgi.dir == FROM_CGI))
                            {
                                if (r->mode_send == CHUNK)
                                {
                                    r->cgi.len_buf = 0;
                                    r->cgi.p = r->cgi_buf + 8;
                                    cgi_set_size_chunk(r);
                                    r->cgi.dir = TO_CLIENT;
                                    r->mode_send = CHUNK_END;
                                    r->sock_timer = 0;
                                }
                                else
                                {
                                    cgi_del_from_list(r);
                                    end_response(r);
                                }
                            }
                            else
                            {
                                print__err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", 
                                        __func__, __LINE__, cgi_poll_fd[i].events, cgi_poll_fd[i].revents);
                                if (r->cgi.op.scgi <= SCGI_READ_HTTP_HEADERS)
                                    r->err = -RS502;
                                else
                                    r->err = -1;
                                cgi_del_from_list(r);
                                end_response(r);
                            }
                            break;
                        default:
                            print__err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
                            r->err = -1;
                            cgi_del_from_list(r);
                            end_response(r);
                            break;
                    }
                }
            }
            /*else if (poll_fd[i].revents == 0)
            {
                // --all; NO!!!!!
            }*/
            ++i;
        }
    }

    return 0;
}
//======================================================================
void *cgi_handler(void *arg)
{
    int num_chld = *((int*)arg);
    n_poll = n_work = 0;
    cgi_poll_fd = malloc(sizeof(struct pollfd) * conf->MaxWorkConnections);
    if (!cgi_poll_fd)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    while (1)
    {
    pthread_mutex_lock(&mtx_);
        while ((!work_list_start) && (!wait_list_start) && (!close_thr))
        {
            pthread_cond_wait(&cond_, &mtx_);
        }
    pthread_mutex_unlock(&mtx_);
        if (close_thr)
            break;
        cgi_add_work_list();
        set_poll_list();
        if (poll_(num_chld) < 0)
        {
            print_err("[%d]<%s:%d> Error poll_()\n", num_chld, __func__, __LINE__);
            break;
        }
    }
    
    free(cgi_poll_fd);
    return NULL;
}
//======================================================================
void push_cgi(Connect *r)
{
    r->operation = DYN_PAGE;
    r->io_status = WORK;
    r->respStatus = RS200;
    r->sock_timer = 0;
    r->prev = NULL;
pthread_mutex_lock(&mtx_);
    r->next = wait_list_start;
    if (wait_list_start)
        wait_list_start->prev = r;
    wait_list_start = r;
    if (!wait_list_end)
        wait_list_end = r;
    ++num_wait;
pthread_mutex_unlock(&mtx_);
    pthread_cond_signal(&cond_);
}
//======================================================================
static int cgi_fork(Connect *r, int* serv_cgi, int* cgi_serv)
{
    struct stat st;

    if (r->reqMethod == M_POST)
    {
        if (r->req_hd.iReqContentType < 0)
        {
            print__err(r, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if (r->req_hd.reqContentLength < 0)
        {
            print__err(r, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
        }

        if (r->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print__err(r, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, r->req_hd.reqContentLength);
            return -RS413;
        }
    }

    switch (r->cgi_type)
    {
        case CGI:
            StrCpy(&r->path, conf->ScriptPath);
            StrCat(&r->path, get_script_name(StrPtr(&r->scriptName)));
            break;
        case PHPCGI:
            StrCpy(&r->path, conf->DocumentRoot);
            StrCat(&r->path, StrPtr(&r->scriptName));
            break;
        default:
            print__err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
            return -RS500;
    }
    
    if (stat(StrPtr(&r->path), &st) == -1)
    {
        print__err(r, "<%s:%d> script (%s) not found\n", __func__, __LINE__, StrPtr(&r->path));
        return -RS404;
    }
    //--------------------------- fork ---------------------------------
    pid_t pid = fork();
    if (pid < 0)
    {
        r->cgi.pid = pid;
        print__err(r, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, strerror(errno));
        return -RS500;
    }
    else if (pid == 0)
    {
        //----------------------- child --------------------------------
        close(cgi_serv[0]);

        if (r->reqMethod == M_POST)
        {
            close(serv_cgi[1]);
            if (serv_cgi[0] != STDIN_FILENO)
            {
                if (dup2(serv_cgi[0], STDIN_FILENO) < 0)
                {
                    fprintf(stderr, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                close(serv_cgi[0]);
            }
        }

        if (cgi_serv[1] != STDOUT_FILENO)
        {
            if (dup2(cgi_serv[1], STDOUT_FILENO) < 0)
            {
                fprintf(stderr, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(cgi_serv[1]);
        }

        if (r->cgi_type == PHPCGI)
            setenv("REDIRECT_STATUS", "true", 1);
        setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("SERVER_SOFTWARE", conf->ServerSoftware, 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("DOCUMENT_ROOT", conf->DocumentRoot, 1);
        setenv("REMOTE_ADDR", r->remoteAddr, 1);
        setenv("REQUEST_URI", r->uri, 1);
        setenv("REQUEST_METHOD", get_str_method(r->reqMethod), 1);
        setenv("SERVER_PROTOCOL", get_str_http_prot(r->httpProt), 1);
        if (r->req_hd.iHost >= 0)
            setenv("HTTP_HOST", r->reqHeadersValue[r->req_hd.iHost], 1);
        if (r->req_hd.iReferer >= 0)
            setenv("HTTP_REFERER", r->reqHeadersValue[r->req_hd.iReferer], 1);
        if (r->req_hd.iUserAgent >= 0)
            setenv("HTTP_USER_AGENT", r->reqHeadersValue[r->req_hd.iUserAgent], 1);

        setenv("SCRIPT_NAME", StrPtr(&r->scriptName), 1);
        setenv("SCRIPT_FILENAME", StrPtr(&r->path), 1);
        if (r->reqMethod == M_POST)
        {
            if (r->req_hd.iReqContentType >= 0)
                setenv("CONTENT_TYPE", r->reqHeadersValue[r->req_hd.iReqContentType], 1);
            if (r->req_hd.iContentLength >= 0)
                setenv("CONTENT_LENGTH", r->reqHeadersValue[r->req_hd.iContentLength], 1);
        }

        setenv("QUERY_STRING", r->sReqParam ? r->sReqParam : "", 1);
        int err_ = 0;
        if (r->cgi_type == CGI)
        {
            execl(StrPtr(&r->path), base_name(StrPtr(&r->scriptName)), NULL);
            err_ = errno;
        }
        else if (r->cgi_type == PHPCGI)
        {
            execl(conf->PathPHP, base_name(conf->PathPHP), NULL);
            err_ = errno;
        }

        char s[64];
        get_time(s, sizeof(s));
        printf( "Status: 500 Internal Server Error\r\n"
                "Content-type: text/html; charset=UTF-8\r\n"
                "\r\n"
                "<!DOCTYPE html>\n"
                "<html>\n"
                " <head>\n"
                "  <title>500 Internal Server Error</title>\n"
                "  <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
                " </head>\n"
                " <body>\n"
                "  <h3>500 Internal Server Error</h3>\n"
                "  <p>%s</p>\n"
                "  <hr>\n"
                "  %s\n"
                " </body>\n"
                "</html>", strerror(err_), s);
        fclose(stdout);
        exit(EXIT_FAILURE);
    }
    else
    {
        r->cgi.pid = pid;
        if (r->req_hd.reqContentLength > 0)
        {
            r->sock_timer = 0;
            r->cgi.len_post = r->req_hd.reqContentLength - r->lenTail;
            r->cgi.op.cgi = CGI_STDIN;
            if (r->lenTail > 0)
            {
                r->cgi.dir = TO_CGI;
                r->cgi.p = r->tail;
                r->cgi.len_buf = r->lenTail;
                r->tail = NULL;
                r->lenTail = 0;
            }
            else
            {
                r->cgi.dir = FROM_CLIENT;
            }
        }
        else
        {
            cgi_set_status_readheaders(r);
        }

        r->tail = NULL;
        r->lenTail = 0;
        r->sock_timer = 0;

        r->mode_send = ((r->httpProt == HTTP11) && r->connKeepAlive) ? CHUNK : NO_CHUNK;
        int opt = 1;
        ioctl(cgi_serv[0], FIONBIO, &opt);
        ioctl(serv_cgi[1], FIONBIO, &opt);
    }

    return 0;
}
//======================================================================
int cgi_create_pipes(Connect *req)
{
    int serv_cgi[2], cgi_serv[2];
    req->cgi.to_script = -1;
    req->cgi.from_script = -1;

    int n = pipe(cgi_serv);
    if (n == -1)
    {
        print__err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
        return -RS500;
    }

    if (req->reqMethod == M_POST)
    {
        n = pipe(serv_cgi);
        if (n == -1)
        {
            print__err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
            close(cgi_serv[0]);
            close(cgi_serv[1]);
            return -RS500;
        }
    }
    else
    {
        serv_cgi[0] = -1;
        serv_cgi[1] = -1;
    }

    n = cgi_fork(req, serv_cgi, cgi_serv);
    if (n < 0)
    {
        if (req->reqMethod == M_POST)
        {
            close(serv_cgi[0]);
            close(serv_cgi[1]);
        }

        close(cgi_serv[0]);
        close(cgi_serv[1]);
        return n;
    }
    else
    {
        if (req->reqMethod == M_POST)
            close(serv_cgi[0]);
        close(cgi_serv[1]);
        
        req->cgi.from_script = cgi_serv[0];
        req->cgi.to_script = serv_cgi[1];
    }

    return 0;
}
//======================================================================
int cgi_stdin(Connect *req)// return [ ERR_TRY_AGAIN | -1 | 0 ]
{
    if (req->cgi.dir == FROM_CLIENT)
    {
        int rd = (req->cgi.len_post > CGI_BUF_SIZE) ? CGI_BUF_SIZE : req->cgi.len_post;
        req->cgi.len_buf = read_from_client(req, req->cgi_buf, rd);
        if (req->cgi.len_buf < 0)
        {
            if (req->cgi.len_buf == ERR_TRY_AGAIN)
                return ERR_TRY_AGAIN;
            return -1;
        }
        else if (req->cgi.len_buf == 0)
        {
            print__err(req, "<%s:%d> Error read()=0\n", __func__, __LINE__);
            return -1;
        }

        req->cgi.len_post -= req->cgi.len_buf;
        req->cgi.dir = TO_CGI;
        req->cgi.p = req->cgi_buf;
    }
    else if (req->cgi.dir == TO_CGI)
    {
        int fd;
        if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
            fd = req->cgi.to_script;
        else if (req->cgi_type == SCGI)
            fd = req->fcgi.fd;
        else
        {
            print__err(req, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(req->cgi_type));
            return -1;
        }

        int n = write(fd, req->cgi.p, req->cgi.len_buf);
        if (n == -1)
        {
            if (errno == EAGAIN)
                return ERR_TRY_AGAIN;
            print__err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        req->cgi.p += n;
        req->cgi.len_buf -= n;

        if (req->cgi.len_buf == 0)
        {
            if (req->cgi.len_post == 0)
            {
                if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
                {
                    close(req->cgi.to_script);
                    req->cgi.to_script = -1;
                    cgi_set_status_readheaders(req);
                }
                else if (req->cgi_type == SCGI)
                {
                    req->cgi.op.scgi = SCGI_READ_HTTP_HEADERS;
                    req->cgi.dir = FROM_CGI;
                    req->tail = NULL;
                    req->lenTail = 0;
                    req->p_newline = req->cgi.p = req->cgi_buf + 8;
                    req->cgi.len_buf = 0;
                }
                else
                {
                    print__err(req, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(req->cgi_type));
                    return -1;
                }
            }
            else
            {
                req->cgi.dir = FROM_CLIENT;
            }
        }
    }

    return 0;
}
//======================================================================
int cgi_stdout(Connect *req)// return [ ERR_TRY_AGAIN | -1 | 0 | 1 | 0< ]
{
    if (req->cgi.dir == FROM_CGI)
    {
        int fd;
        if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
            fd = req->cgi.from_script;
        else
            fd = req->fcgi.fd;
        req->cgi.len_buf = read(fd, req->cgi_buf + 8, CGI_BUF_SIZE);
        if (req->cgi.len_buf == -1)
        {
            if (errno == EAGAIN)
                return ERR_TRY_AGAIN;
            print__err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (req->cgi.len_buf == 0)
        {
            if (req->mode_send == CHUNK)
            {
                req->cgi.len_buf = 0;
                req->cgi.p = req->cgi_buf + 8;
                cgi_set_size_chunk(req);
                req->cgi.dir = TO_CLIENT;
                req->mode_send = CHUNK_END;
                return req->cgi.len_buf;
            }
            return 0;
        }

        req->cgi.dir = TO_CLIENT;
        if (req->mode_send == CHUNK)
        {
            req->cgi.p = req->cgi_buf + 8;
            if (cgi_set_size_chunk(req))
                return -1;
        }
        else
            req->cgi.p = req->cgi_buf + 8;
        return req->cgi.len_buf;
    }
    else if (req->cgi.dir == TO_CLIENT)
    {
        int ret = write_to_client(req, req->cgi.p, req->cgi.len_buf);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                return ERR_TRY_AGAIN;
            return -1;
        }

        req->cgi.p += ret;
        req->cgi.len_buf -= ret;
        req->send_bytes += ret;
        if (req->cgi.len_buf == 0)
        {
            if (req->mode_send == CHUNK_END)
                return 0;
            req->cgi.dir = FROM_CGI;
        }
    }

    return 1;
}
//======================================================================
void close_cgi_handler(void)
{
    close_thr = 1;
    pthread_cond_signal(&cond_);
}
//======================================================================
int cgi_find_empty_line(Connect *req)
{
    char *pCR, *pLF;
    while (req->lenTail > 0)
    {
        int i = 0, len_line = 0;
        pCR = pLF = NULL;
        while (i < req->lenTail)
        {
            char ch = *(req->p_newline + i);
            if (ch == '\r')// found CR
            {
                if (i == (req->lenTail - 1))
                    return 0;
                if (pCR)
                    return -RS502;
                pCR = req->p_newline + i;
            }
            else if (ch == '\n')// found LF
            {
                pLF = req->p_newline + i;
                if ((pCR) && ((pLF - pCR) != 1))
                    return -RS502;
                i++;
                break;
            }
            else
                len_line++;
            i++;
        }

        if (pLF) // found end of line '\n'
        {
            if (pCR == NULL)
                *pLF = 0;
            else
                *pCR = 0;

            if (len_line == 0)
            {
                req->lenTail -= i;
                if (req->lenTail > 0)
                    req->tail = pLF + 1;
                else
                    req->tail = NULL;
                return 1;
            }
///fprintf(stderr, "<%s:%d> [%s]\n", __func__, __LINE__, req->p_newline);
            if (!memchr(req->p_newline, ':', len_line))
            {
                //print__err(req, "<%s:%d> Error Line not header: [%s]\n", __func__, __LINE__, req->p_newline);
                return -RS502;
            }

            if (!strlcmp_case(req->p_newline, "Status", 6))
            {
                req->respStatus = atoi(req->p_newline + 7);
            }
            else
                StrCatLN(&req->hdrs, req->p_newline);

            req->lenTail -= i;
            req->p_newline = pLF + 1;
        }
        else if (pCR && (!pLF))
            return -RS502;
        else
            break;
    }

    return 0;
}
//======================================================================
int cgi_read_http_headers(Connect *req)
{
    int num_read = CGI_BUF_SIZE - req->cgi.len_buf - 1;
    if (num_read <= 0)
        return -RS502;
    //num_read = (num_read > 16) ? 16 : num_read;
    int fd;
    if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
        fd = req->cgi.from_script;
    else
        fd = req->fcgi.fd;

    int n = read(fd, req->cgi.p, num_read);
    if (n == -1)
    {
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        print__err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (n == 0)
    {
        print__err(req, "<%s:%d> Error read()=0\n", __func__, __LINE__);
        return -1;
    }

    req->lenTail += n;
    req->cgi.len_buf += n;
    req->cgi.p += n;
    *(req->cgi.p) = 0;

    n = cgi_find_empty_line(req);
    if (n == 1) // empty line found
        return req->cgi.len_buf;
    else if (n < 0) // error
        return n;

    return 0;
}
//======================================================================
int cgi_set_size_chunk(Connect *r)
{
    int size = r->cgi.len_buf;
    const char *hex = "0123456789ABCDEF";

    memcpy(r->cgi.p + r->cgi.len_buf, "\r\n", 2);
    int i = 7;
    *(--r->cgi.p) = '\n';
    *(--r->cgi.p) = '\r';
    i -= 2;
    for ( ; i >= 0; --i)
    {
        *(--r->cgi.p) = hex[size % 16];
        size /= 16;
        if (size == 0)
            break;
    }

    if (size != 0)
        return -1;
    r->cgi.len_buf += (8 - i + 2);

    return 0;
}
//======================================================================
static void cgi_set_poll_list(Connect *r, int *i)
{
    if (r->cgi.dir == FROM_CLIENT)
    {
        r->timeout = conf->Timeout;
        cgi_poll_fd[*i].fd = r->clientSocket;
        cgi_poll_fd[*i].events = POLLIN;
    }
    else if (r->cgi.dir == TO_CLIENT)
    {
        r->timeout = conf->Timeout;
        cgi_poll_fd[*i].fd = r->clientSocket;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->cgi.dir == FROM_CGI)
    {
        r->timeout = conf->TimeoutCGI;
        cgi_poll_fd[*i].fd = r->cgi.from_script;
        cgi_poll_fd[*i].events = POLLIN;
    }
    else if (r->cgi.dir == TO_CGI)
    {
        r->timeout = conf->TimeoutCGI;
        cgi_poll_fd[*i].fd = r->cgi.to_script;
        cgi_poll_fd[*i].events = POLLOUT;
    }

    (*i)++;
}
//======================================================================
static void cgi_worker(Connect* r)
{
    if (r->cgi.op.cgi == CGI_STDIN)
    {
        int ret = cgi_stdin(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }
        }
    }
    else if (r->cgi.op.cgi == CGI_READ_HTTP_HEADERS)
    {
        int ret = cgi_read_http_headers(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }
        }
        else if (ret > 0)
        {
            if (create_response_headers(r))
            {
                print__err(r, "<%s:%d> Error create_response_headers()\n", __func__, __LINE__);
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
            }
            else
            {
                r->cgi.op.cgi = CGI_SEND_HTTP_HEADERS;
                r->sock_timer = 0;
            }
        }
        else
            r->sock_timer = 0;
    }
    else if (r->cgi.op.cgi == CGI_SEND_HTTP_HEADERS)
    {
        if (r->resp_headers.len > 0)
        {
            int wr = write_to_client(r, r->resp_headers.ptr + r->resp_headers.ind, r->resp_headers.len - r->resp_headers.ind);
            if (wr < 0)
            {
                if (wr == ERR_TRY_AGAIN)
                    r->io_status = POLL;
                else
                {
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                    close(r->cgi.from_script);
                    r->cgi.from_script = -1;
                    r->err = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else
            {
                r->resp_headers.ind += wr;
                if ((r->resp_headers.len - r->resp_headers.ind) == 0)
                {
                    if (r->reqMethod == M_HEAD)
                    {
                        close(r->cgi.from_script);
                        r->cgi.from_script = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                    }
                    else
                    {
                        r->cgi.op.cgi = CGI_SEND_ENTITY;
                        r->sock_timer = 0;
                        if (r->lenTail > 0)
                        {
                            r->cgi.p = r->tail;
                            r->cgi.len_buf = r->lenTail;
                            r->lenTail = 0;
                            r->tail = NULL;
                            r->cgi.dir = TO_CLIENT;
                            if (r->mode_send == CHUNK)
                            {
                                if (cgi_set_size_chunk(r))
                                {
                                    r->err = -1;
                                    cgi_del_from_list(r);
                                    end_response(r);
                                }
                            }
                        }
                        else
                        {
                            r->cgi.len_buf = 0;
                            r->cgi.p = NULL;
                            r->cgi.dir = FROM_CGI;
                        }
                    }
                }
                else
                    r->sock_timer = 0;
            }
        }
        else
        {
            print__err(r, "<%s:%d> Error resp.len=%d\n", __func__, __LINE__, r->resp_headers.len);
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHeadersValue[r->req_hd.iReferer] = "Error send response headers";
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->cgi.op.cgi == CGI_SEND_ENTITY)
    {
        int ret = cgi_stdout(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
            }
        }
        else if (ret == 0)
        {
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else
    {
        print__err(r, "<%s:%d> ??? Error: CGI_OPERATION=%s\n", __func__, __LINE__, get_cgi_operation(r->cgi.op.cgi));
        r->err = -1;
        cgi_del_from_list(r);
        end_response(r);
    }
}
//======================================================================
void cgi_set_status_readheaders(Connect *r)
{
    r->cgi.op.cgi = CGI_READ_HTTP_HEADERS;
    r->cgi.dir = FROM_CGI;
    r->tail = NULL;
    r->lenTail = 0;
    r->p_newline = r->cgi.p = r->cgi_buf + 8;
    r->cgi.len_buf = 0;
    r->sock_timer = 0;
}
//======================================================================
int timeout_cgi(Connect *r)
{
    if ((r->cgi.op.cgi == CGI_STDIN) && (r->cgi.dir == TO_CGI))
        return -RS504;
    else if (r->cgi.op.cgi == CGI_READ_HTTP_HEADERS)
        return -RS504;
    else
        return -1;
}



