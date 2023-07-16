#include "server.h"

static Connect *list_start = NULL;
static Connect *list_end = NULL;

pthread_mutex_t mtx_thr = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_list = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_new_thr = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_exit_thr = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mtx_conn = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_close_conn = PTHREAD_COND_INITIALIZER;

static int count_conn = 0, all_req = 0;
int stop_manager = 0;

int get_num_cgi();

static int nProc;
//======================================================================
int get_num_chld(void)
{
    return nProc;
}
//======================================================================
int get_num_conn(void)
{
pthread_mutex_lock(&mtx_conn);
    int n = count_conn;
pthread_mutex_unlock(&mtx_conn);
    return n;
}
//======================================================================
void start_conn(void)
{
pthread_mutex_lock(&mtx_conn);
    ++count_conn;
pthread_mutex_unlock(&mtx_conn);
}
//======================================================================
void push_resp_list(Connect *req)
{
pthread_mutex_lock(&mtx_thr);
    req->next = NULL;
    req->prev = list_end;
    if (list_start)
    {
        list_end->next = req;
        list_end = req;
    }
    else
        list_start = list_end = req;

    ++all_req;
pthread_mutex_unlock(&mtx_thr);
    pthread_cond_signal(&cond_list);
}
//======================================================================
Connect *pop_resp_list(void)
{
pthread_mutex_lock(&mtx_thr);

    while ((list_start == NULL) && (!stop_manager))
    {
        pthread_cond_wait(&cond_list, &mtx_thr);
    }

    Connect *req = list_start;
    if (list_start && list_start->next)
    {
        list_start->next->prev = NULL;
        list_start = list_start->next;
    }
    else
        list_start = list_end = NULL;

pthread_mutex_unlock(&mtx_thr);
    return req;
}
//======================================================================
void close_manager()
{
    stop_manager = 1;
    pthread_cond_broadcast(&cond_list);
}
//======================================================================
static int fd_close_conn;
//======================================================================
void end_response(Connect *req)
{
    free_strings_request(req);
    free_range(req);
    free_fcgi_param(req);
    if (req->connKeepAlive == 0 || req->err < 0)
    { // ----- Close connect -----
        if (req->err <= -RS101)
        {
            req->respStatus = -req->err;
            req->err = 0;
            if (create_message(req, NULL) == 1)
                return;
        }

        if (req->operation != READ_REQUEST)
            print_log(req);

        shutdown(req->clientSocket, SHUT_RDWR);
        close(req->clientSocket);
        free(req);
    pthread_mutex_lock(&mtx_conn);
        --count_conn;
    pthread_mutex_unlock(&mtx_conn);
        char ch = nProc;
        if (write(fd_close_conn, &ch, 1) <= 0)
        {
            print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            exit(1);
        }
        pthread_cond_broadcast(&cond_close_conn);
    }
    else
    { // ----- KeepAlive -----
    #ifdef TCP_CORK_
        if (conf->TcpCork == 'y')
        {
        #if defined(LINUX_)
            int optval = 0;
            setsockopt(req->clientSocket, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
        #elif defined(FREEBSD_)
            int optval = 0;
            setsockopt(req->clientSocket, IPPROTO_TCP, TCP_NOPUSH, &optval, sizeof(optval));
        #endif
        }
    #endif
        print_log(req);
        init_struct_request(req);
        init_strings_request(req);
        req->timeout = conf->TimeoutKeepAlive;
        ++req->numReq;
        req->operation = READ_REQUEST;
        push_pollin_list(req);
    }
}
//======================================================================
void *thread_client(void *req);
//======================================================================
int create_thread(int *num_proc)
{
    int n;
    pthread_t thr;

    n = pthread_create(&thr, NULL, thread_client, num_proc);
    if (n)
    {
        print_err("[%d]<%s:%d> Error pthread_create(): %d\n", *num_proc, __func__, __LINE__, n);
        return n;
    }

    n = pthread_detach(thr);
    if (n)
    {
        print_err("[%d]<%s:%d> Error pthread_detach(): %d\n", *num_proc, __func__, __LINE__, n);
        exit(n);
    }

    return 0;
}
//======================================================================
static int servSock, uxSock;
static Connect *create_req(void);
//======================================================================
static void sig_handler_child(int sig)
{
    if (sig == SIGINT)
    {
        fprintf(stderr, "[%d]<%s:%d> ### SIGINT ### all req: %d\n", nProc, __func__, __LINE__, all_req);
    }
    else if (sig == SIGSEGV)
    {
        fprintf(stderr, "[%d]<%s:%d> ### SIGSEGV ###\n", nProc, __func__, __LINE__);
        shutdown(uxSock, SHUT_RDWR);
        close(uxSock);
        exit(1);
    }
    else
        fprintf(stderr, "[%d]<%s:%d> ### SIG=%d ###\n", nProc, __func__, __LINE__, sig);
}
//======================================================================
int manager(int sockServer, int numProc, int unixSock, int to_parent)
{
    unsigned long allConn = 1;
    //------------------------------------------------------------------
    uxSock = unixSock;

    fd_close_conn = to_parent;
    nProc = numProc;
    servSock = sockServer;
    //------------------------------------------------------------------
    if (signal(SIGINT, sig_handler_child) == SIG_ERR)
    {
        print_err("<%s:%d> Error signal(SIGINT): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGSEGV, sig_handler_child) == SIG_ERR)
    {
        print_err("<%s:%d> Error signal(SIGSEGV): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    if (chdir(conf->DocumentRoot))
    {
        print_err("[%d] <%s:%d> Error chdir(%s): %s\n", numProc, __func__, __LINE__, conf->DocumentRoot, strerror(errno));
        exit(1);
    }
    //------------------------------------------------------------------
    pthread_t thr_handler;
    int err = pthread_create(&thr_handler, NULL, event_handler, &numProc);
    if (err)
    {
        print_err("<%s:%d> Error pthread_create(event_handler): %s\n", __func__, __LINE__, strerror(err));
        exit(1);
    }
    //------------------------------------------------------------------
    pthread_t thr_cgi;
    err = pthread_create(&thr_cgi, NULL, cgi_handler, &numProc);
    if (err)
    {
        print_err("<%s:%d> Error pthread_create(cgi_handler): %s\n", __func__, __LINE__, strerror(err));
        exit(1);
    }
    //------------------------------------------------------------------
    int n;
    for (n = 0; n < conf->NumThreads; ++n)
    {
        err = create_thread(&numProc);
        if (err)
        {
            print_err("<%s:%d> Error create_thread() %d(%d)\n", __func__, __LINE__, err, conf->NumThreads);
            exit(1);
        }
    }

    printf("[%d] +++++ num threads=%d, pid=%d, uid=%d, gid=%d +++++\n", numProc,
                                n, getpid(), getuid(), getgid());
    //------------------------------------------------------------------
    while (1)
    {
        struct sockaddr_storage clientAddr;
        socklen_t addrSize = sizeof(struct sockaddr_storage);
        char data[1] = "";
        int sz = sizeof(data);

        int clientSocket = recv_fd(unixSock, numProc, data, (int*)&sz);
        if (clientSocket < 0)
        {
            //print_err("[%d]<%s:%d> Error recv_fd()\n", numProc, __func__, __LINE__);
            break;
        }

        Connect *req;
        req = create_req();
        if (!req)
        {
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            break;
        }

        int opt = 1;
        ioctl(clientSocket, FIONBIO, &opt);

        init_struct_request(req);
        init_strings_request(req);
        req->numProc = numProc;
        req->numConn = allConn++;
        req->numReq = 1;
        req->serverSocket = sockServer;
        req->clientSocket = clientSocket;
        req->timeout = conf->Timeout;
        req->remoteAddr[0] = '\0';
        getpeername(clientSocket,(struct sockaddr *)&clientAddr, &addrSize);
        n = getnameinfo((struct sockaddr *)&clientAddr,
                addrSize,
                req->remoteAddr,
                sizeof(req->remoteAddr),
                NULL,
                0,
                NI_NUMERICHOST);
        if (n != 0)
            print__err(req, "<%s> Error getnameinfo()=%d: %s\n", __func__, n, gai_strerror(n));

        req->operation = READ_REQUEST;
        start_conn();
        push_pollin_list(req);
    }

    print_err("<%d> <%s:%d> all_req=%u; open_conn=%d\n", numProc, __func__, __LINE__, all_req, count_conn);

    close_manager();

    close_event_handler();
    close_cgi_handler();

    pthread_join(thr_handler, NULL);
    pthread_join(thr_cgi, NULL);

    free_fcgi_list();
    usleep(100000);
    return 0;
}
//======================================================================
Connect *create_req()
{
    Connect *req = NULL;

    req = malloc(sizeof(Connect));
    if (!req)
        print_err("<%s:%d> Error malloc(): %s(%d)\n", __func__, __LINE__, strerror(errno), errno);
    return req;
}
