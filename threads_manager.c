#include "server.h"

static Connect *list_start = NULL;
static Connect *list_end = NULL;

pthread_mutex_t mtx_thr = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_list = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_new_thr = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_exit_thr = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mtx_conn = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_close_conn = PTHREAD_COND_INITIALIZER;

static int count_thr = 0, count_conn = 0, size_list = 0, all_thr = 0, num_wait_thr = 0;
int stop_manager = 0, need_create_thr = 0;

int get_num_cgi();

static int nChld;
//======================================================================
int get_num_chld(void)
{
    return nChld;
}
//======================================================================
int get_num_thr(void)
{
pthread_mutex_lock(&mtx_thr);
    int ret = count_thr;
pthread_mutex_unlock(&mtx_thr);
    return ret;
}
//======================================================================
int get_all_thr(void)
{
    return all_thr;
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
int start_thr(void)
{
pthread_mutex_lock(&mtx_thr);
    int ret = ++count_thr;
pthread_mutex_unlock(&mtx_thr);
    return ret;
}
//======================================================================
void wait_exit_thr(int n)
{
pthread_mutex_lock(&mtx_thr);
    while (n == count_thr)
    {
        pthread_cond_wait(&cond_exit_thr, &mtx_thr);
    }
pthread_mutex_unlock(&mtx_thr);
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
    
    ++size_list;
pthread_mutex_unlock(&mtx_thr);
    pthread_cond_signal(&cond_list);
}
//======================================================================
Connect *pop_resp_list(void)
{
pthread_mutex_lock(&mtx_thr);
    ++num_wait_thr;
    while ((list_start == NULL) && (!stop_manager))
    {
        pthread_cond_wait(&cond_list, &mtx_thr);
    }
    --num_wait_thr;
    Connect *req = list_start;
    if (list_start && list_start->next)
    {
        list_start->next->prev = NULL;
        list_start = list_start->next;
    }
    else
        list_start = list_end = NULL;
    --size_list;
pthread_mutex_unlock(&mtx_thr);
    if (num_wait_thr <= 1)
        pthread_cond_signal(&cond_new_thr);
    return req;
}
//======================================================================
int wait_create_thr(int *n)
{
pthread_mutex_lock(&mtx_thr);
    while (((size_list <= num_wait_thr) || (count_thr >= conf->MaxThreads)) && !stop_manager)
    {
        pthread_cond_wait(&cond_new_thr, &mtx_thr);
    }
    
    *n = count_thr;
pthread_mutex_unlock(&mtx_thr);
    return stop_manager;
}
//======================================================================
int end_thr(int ret)
{
pthread_mutex_lock(&mtx_thr);
    if (((count_thr > conf->MinThreads) && (size_list < num_wait_thr)) || ret)
    {
        --count_thr;
        ret = EXIT_THR;
    }
pthread_mutex_unlock(&mtx_thr);
    if (ret)
    {
        pthread_cond_broadcast(&cond_exit_thr);
    }
    return ret;
}
//======================================================================
void close_manager()
{
    stop_manager = 1;
    pthread_cond_signal(&cond_new_thr);
    pthread_cond_signal(&cond_exit_thr);
    pthread_cond_broadcast(&cond_list);
}
//======================================================================
static int fd_close_conn;
//======================================================================
void end_response(Connect *req)
{
    if (req->connKeepAlive == 0 || req->err < 0)
    { // ----- Close connect -----
        if (req->err > NO_PRINT_LOG)
            print_log(req);
        shutdown(req->clientSocket, SHUT_RDWR);
        close(req->clientSocket);
        free(req);
    pthread_mutex_lock(&mtx_conn);
        --count_conn;
    pthread_mutex_unlock(&mtx_conn);
        char ch = nChld;
        if (write(fd_close_conn, &ch, 1) <= 0)
        {
            print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            exit(1);
        }
        pthread_cond_broadcast(&cond_close_conn);
    }
    else
    { // ----- KeepAlive -----
        if (conf->tcp_cork == 'y')
        {
            int optval = 0;
            setsockopt(req->clientSocket, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
        }
        
        print_log(req);
        req->timeout = conf->TimeoutKeepAlive;
        ++req->numReq;
        push_pollin_list(req);
    }
}
//======================================================================
int check_num_conn()
{
pthread_mutex_lock(&mtx_conn);
    while (count_conn >= conf->MAX_REQUESTS)
    {
        pthread_cond_wait(&cond_close_conn, &mtx_conn);
    }
pthread_mutex_unlock(&mtx_conn);
    return 0;
}
//======================================================================
void *thread_client(void *req);
//======================================================================
int create_thread(int *num_chld)
{
    int n;
    pthread_t thr;

    n = pthread_create(&thr, NULL, thread_client, num_chld);
    if (n)
    {
        // errno = 12; n = 11
        print_err("<%s> Error pthread_create(): %d\n", __func__, n);
        return n;
    }

    n = pthread_detach(thr);
    if (n)
    {
        print_err("<%s> Error pthread_detach(): %d\n", __func__, n);
        exit(n);
    }

    return 0;
}
//======================================================================
void *thr_create_manager(void *par)
{
    int num_chld = *((int*)par);
    int num_thr;

    while (1)
    {
        if (wait_create_thr(&num_thr))
            break;

        int n = create_thread(&num_chld);
        if (n)
        {
            print_err("[%d] <%s:%d> Error create thread: num_thr=%d, all_thr=%u, errno=%d\n", num_chld, __func__, __LINE__, num_thr, all_thr, n);
            wait_exit_thr(num_thr);
        }
        else
        {
            ++all_thr;
            start_thr();
        }
    }

    return NULL;
}
//======================================================================
int servSock, uxSock;
Connect *create_req(void);
//======================================================================
static void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        //print_err("[%d] <%s:%d> ### SIGINT ###\n", nChld, __func__, __LINE__);
        shutdown(servSock, SHUT_RDWR);
        close(servSock);
        close(uxSock);
    }
    else if (sig == SIGSEGV)
    {
        print_err("[%d] <%s:%d> ### SIGSEGV ###\n", nChld, __func__, __LINE__);
        exit(1);
    }
}
//======================================================================
int manager(int sockServer, int numChld, int to_parent)
{
    int n;
    unsigned long allConn = 0, i;
    int num_thr;
    pthread_t thr_handler, thr_man;
    int par[3];
    char nameSock[32];
    snprintf(nameSock, sizeof(nameSock), "unix_sock_%d", numChld);
    
    if (remove(nameSock) == -1 && errno != ENOENT)
    {
        print_err("[%n]<%s:%d> Error remove(%s): %s\n", numChld, __func__, __LINE__, nameSock, strerror(errno));
        exit(1);
    }
    
    int unixSock = unixBind(nameSock, SOCK_DGRAM);
    if (unixSock == -1)
    {
        print_err("[%u]<%s:%d> Error unixBind()=%d\n", numChld, __func__, __LINE__, unixSock);
        exit(1);
    }
    
    uxSock = unixSock;

    fd_close_conn = to_parent;
    nChld = numChld;
    servSock = sockServer;
    
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    
    if (signal(SIGINT, signal_handler) == SIG_ERR)
    {
        print_err("<%s:%d> Error signal(SIGINT): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (signal(SIGSEGV, signal_handler) == SIG_ERR)
    {
        print_err("<%s:%d> Error signal(SIGSEGV): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    if (chdir(conf->rootDir))
    {
        print_err("[%d] <%s:%d> Error chdir(%s): %s\n", numChld, __func__, __LINE__, conf->rootDir, strerror(errno));
        exit(1);
    }
    //------------------------------------------------------------------
    for (i = 0; i < conf->MinThreads; ++i)
    {
        n = create_thread(&numChld);
        if (n)
        {
            print_err("<%s:%d> Error create_thread() %d \n", __func__, __LINE__, i);
            break;
        }
        num_thr = start_thr();
    }
    
    if (get_num_thr() > conf->MinThreads)
    {
        print_err("[%d:%s:%d] Error num threads=%d\n", numChld, __func__, __LINE__, get_num_thr());
        exit(1);
    }
    printf("[%d:%s:%d] +++++ num threads=%d, pid=%d +++++\n", numChld, __func__, __LINE__, get_num_thr(), getpid());
    all_thr = num_thr;
    par[0] = numChld;
    //------------------------------------------------------------------
    n = pthread_create(&thr_handler, NULL, event_handler, par);
    if (n)
    {
        printf("<%s:%d> Error pthread_create(send_file_): %s\n", __func__, __LINE__, strerror(n));
        exit(1);
    }
    //------------------------------------------------------------------
    n = pthread_create(&thr_man, NULL, thr_create_manager, par);
    if (n)
    {
        printf("<%s> Error pthread_create(): %s\n", __func__, strerror(n));
        exit(1);
    }

    while (1)
    {
        struct sockaddr_storage clientAddr;
        socklen_t addrSize = sizeof(struct sockaddr_storage);
        
        int clientSocket = recv_fd(unixSock, numChld, &clientAddr, addrSize);
        if (clientSocket < 0)
        {
            print_err("[%d]<%s:%d> Error recv_fd()\n", numChld, __func__, __LINE__);
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
        
        req->numChld = numChld;
        req->numConn = allConn++;
        req->numReq = 0;
        req->serverSocket = sockServer;
        req->clientSocket = clientSocket;
        req->timeout = conf->TimeOut;
        req->remoteAddr[0] = '\0';
        getnameinfo((struct sockaddr *)&clientAddr, 
                addrSize, 
                req->remoteAddr, 
                sizeof(req->remoteAddr),
                NULL, 
                0, 
                NI_NUMERICHOST);

        start_conn();
        push_pollin_list(req);
    }
    
    print_err("<%d> thr=%d, allThr=%d, open_conn=%d, allConn=%d\n", numChld, count_thr, all_thr, count_conn, allConn);
    i = count_thr;
    
    close_manager();
    pthread_join(thr_man, NULL);
    
    close_event_handler();
    pthread_join(thr_handler, NULL);

    free_fcgi_list();
    sleep(1);
    return 0;
}
//======================================================================
Connect *create_req(void)
{
    Connect *req = NULL;
    
    req = malloc(sizeof(Connect));
    if (!req)
        print_err("<%s:%d> Error malloc(): %s(%d)\n", __func__, __LINE__, str_err(errno), errno);
    return req;
}
