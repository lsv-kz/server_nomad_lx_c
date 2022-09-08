#include "server.h"

static int sockServer, sock_sig;

int create_server_socket(const Config *conf);
int read_conf_file(const char *path_conf);
void create_logfiles(const char *log_dir, const char * ServerSoftware);
int set_uid();
int main_proc();
int seng_fd_timeout(int, int, struct sockaddr_storage *, socklen_t, int);

static int from_chld[2], unixFD[8];
static pid_t pidArr[8];
static int numConn[8];
static char conf_dir[512];
static int start = 0;
static int run = 1;
static unsigned int all_conn = 0;
static char name_sig_sock[32];
static char pidFile[MAX_PATH];
static char received_sig = 0;
enum { RESTART_SIG_ = 1, CLOSE_SIG_,};
//======================================================================
static int qu_init();
static void qu_push(int s);
void *thread_send_socket(void *arg);
static void close_queue_sock();
static int qu_not_full();
static void set_arr_conn(int n, const unsigned char *s);
//======================================================================
static void sig_handler(int sig)
{
    if (sig == SIGINT)
    {
        fprintf(stderr, "<main> ######  SIGINT  ######\n");
        received_sig = CLOSE_SIG_;
    }
    else if (sig == SIGSEGV)
    {
        fprintf(stderr, "<main> ######  SIGSEGV  ######\n");
        exit(1);
    }
    else
    {
        fprintf(stderr, "sig=%d\n", sig);
    }
}
//======================================================================
int send_sig(const char *sig)
{
    for (int i = 0; i < conf->NUM_PROC; ++i)
    {
        int ret = send_fd(unixFD[i], -1, &i, sizeof(i));
        if (ret < 0)
        {
            fprintf(stderr, "<%s:%d> Error sendClientSock()\n", __func__, __LINE__);
            kill(pidArr[i], SIGKILL);
            return 0;
        }
    }

    if (!strcmp(sig, "restart"))
        return 1;
    else if (!strcmp(sig, "close"))
        return 0;
    else
    {
        fprintf(stderr, "<%s:%d> Error signal: %s\n", __func__, __LINE__, sig);
        return 0;
    }

    return 0;
}
//======================================================================
pid_t create_child(int num_chld, int *from_chld);
//======================================================================
void create_proc(int NumProc)
{
    if (pipe(from_chld) < 0)
    {
        fprintf(stderr, "<%s:%d> Error pipe(): %s\n", __func__, __LINE__, strerror(errno));
        exit(1);
    }
    //------------------------------------------------------------------
    pid_t pid_child;
    int i = 0;
    while (i < NumProc)
    {
        char s[32];
        snprintf(s, sizeof(s), "unix_sock_%d", i);

        pid_child = create_child(i, from_chld);
        if (pid_child < 0)
        {
            fprintf(stderr, "[%d]<%s:%d> Error create_child()\n", i, __func__, __LINE__);
            exit(1);
        }
        pidArr[i] = pid_child;
        ++i;
    }

    close(from_chld[1]);
    sleep(1);
    i = 0;
    while (i < NumProc)
    {
        char s[32];
        snprintf(s, sizeof(s), "unix_sock_%d", i);
        
        if ((unixFD[i] = unixConnect(s, SOCK_DGRAM)) < 0)
        {
            fprintf(stderr, "[%d]<%s:%d> Error create_fcgi_socket(%s)=%d: %s\n", i, __func__, __LINE__, 
                            s, unixFD[i], strerror(errno));
            exit(1);
        }

        if (remove(s) == -1)
        {
            fprintf(stderr, "[%d]<%s:%d> Error remove(%s): %s\n", i, __func__, __LINE__, s, strerror(errno));
            exit(1);
        }
        ++i;
    }
}
//======================================================================
void print_help(const char *name)
{
    fprintf(stderr, "Usage: %s [-c configfile] [-s signal]\n   signals: restart, close\n", name);
    exit(EXIT_FAILURE);
}
//======================================================================
int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc == 1)
        snprintf(conf_dir, sizeof(conf_dir), "%s", "./server.conf");
    else
    {
        int c;
        pid_t pid_ = 0;
        char *sig = NULL, *conf_dir_ = NULL;
        while ((c = getopt(argc, argv, "c:s:h")) != -1)
        {
            switch (c)
            {
                case 'c':
                    conf_dir_ = optarg;
                    break;
                case 's':
                    sig = optarg;
                    break;
                case 'h':
                    print_help(argv[0]);
                    exit(0);
                default:
                    print_help(argv[0]);
                    exit(0);
            }
        }

        if (conf_dir_)
            snprintf(conf_dir, sizeof(conf_dir), "%s", conf_dir_);
        else
            snprintf(conf_dir, sizeof(conf_dir), "%s", "./server.conf");

        if (sig)
        {
            if (read_conf_file(conf_dir))
                exit(1);
            
            snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->PIDDIR);
            FILE *fpid = fopen(pidFile, "r");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile, strerror(errno));
                exit(1);
            }

            fscanf(fpid, "%u", &pid_);
            fclose(fpid);

            int data;
            if (!strcmp(sig, "restart"))
                data = RESTART_SIG_;
            else if (!strcmp(sig, "close"))
                data = CLOSE_SIG_;
            else
            {
                fprintf(stderr, "<%d> ? option -s: %s\n", __LINE__, sig);
                print_help(argv[0]);
                exit(1);
            }

            snprintf(name_sig_sock, sizeof(name_sig_sock), "/tmp/server_sock_sig_%d", pid_);
            int sock = unixConnect(name_sig_sock, SOCK_DGRAM);
            if (sock < 0)
                exit(1);

            if (write(sock, &data, sizeof(data)) < 0)
            {
                fprintf(stderr, "<%d> Error write(): %s\n", __LINE__, strerror(errno));
                close(sock);
                exit(1);
            }

            close(sock);
            exit(0);
        }
    }

    while (run)
    {
        if (read_conf_file(conf_dir))
            return -1;

        snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->PIDDIR);
        FILE *fpid = fopen(pidFile, "w");
        if (!fpid)
        {
            fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile, strerror(errno));
            return -1;
        }

        fprintf(fpid, "%u\n", getpid());
        fclose(fpid);

        if (start == 0)
        {
            sockServer = create_server_socket(conf);
            if (sockServer == -1)
            {
                fprintf(stderr, "<%s:%d> Error: create_server_socket(%s:%s)\n", __func__, __LINE__, conf->SERVER_ADDR, conf->SERVER_PORT);
                exit(1);
            }

            snprintf(name_sig_sock, sizeof(name_sig_sock), "/tmp/server_sock_sig_%d", getpid());
            sock_sig = unixBind(name_sig_sock, SOCK_DGRAM);
            if (sock_sig < 0)
            {
                exit(1);
            }

            set_uid();
            start = 1;
        }

        if (main_proc())
            break;
    }

    return 0;
}
//======================================================================
int main_proc()
{
    char s[256];

    create_logfiles(conf->LOGDIR, conf->SERVER_SOFTWARE);
    //------------------------------------------------------------------
    pid_t pid = getpid();

    get_time(s, 64);
    printf(     " [%s] - server \"%s\" run\n"
                "   ip = %s\n"
                "   port = %s\n\n"

                "   ListenBacklog = %d\n"
                "   tcp_cork = %c\n"
                "   TcpNoDelay = %c\n\n"

                "   SendFile = %c\n"
                "   SndBufSize = %d\n\n"

                "   SizeQueueConnect = %d\n"
                "   MaxWorkConnect = %d\n"
                "   MaxEventConnect = %d\n\n"

                "   NumChld = %d\n"
                "   MaxThreads = %d\n"
                "   MimThreads = %d\n"
                "   MaxProcCgi = %d\n\n"

                "   KeepAlive %c\n"
                "   TimeoutKeepAlive = %d\n"
                "   TimeOut = %d\n"
                "   TimeOutCGI = %d\n"
                "   TimeoutPoll = %d\n\n"

                "   MaxRanges = %d\n\n"

                "   php: %s\n"
                "   path_php: %s\n\n"

                "   ShowMediaFiles = %c\n"
                "   ClientMaxBodySize = %ld\n\n"

                "   index.html = %c\n"
                "   index.php = %c\n"
                "   index.pl = %c\n"
                "   index.fcgi = %c\n",
                s, conf->SERVER_SOFTWARE, conf->SERVER_ADDR, conf->SERVER_PORT, conf->LISTEN_BACKLOG, conf->tcp_cork, 
                conf->tcp_nodelay, conf->SEND_FILE, conf->SNDBUF_SIZE, conf->SIZE_QUEUE_CONNECT, conf->MAX_WORK_CONNECT,  
                conf->MAX_EVENT_CONNECT, conf->NUM_PROC, conf->MAX_THREADS, conf->MIN_THREADS, conf->MAX_PROC_CGI,  
                conf->KEEP_ALIVE, conf->TIMEOUT_KEEP_ALIVE, conf->TIMEOUT, conf->TIMEOUT_CGI, conf->TIMEOUT_POLL, 
                conf->MAX_RANGES, conf->UsePHP, conf->PathPHP, conf->SHOW_MEDIA_FILES, conf->CLIENT_MAX_BODY_SIZE, 
                conf->index_html, conf->index_php, conf->index_pl, conf->index_fcgi);
    printf("   %s;\n   %s\n\n", conf->ROOTDIR, conf->CGIDIR);

    fcgi_list_addr *i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        fprintf(stdout, "   [%s] : [%s]\n", i->scrpt_name, i->addr);
    }

    for ( ; environ[0]; )
    {
        char *p, buf[512];
        if ((p = (char*)memccpy(buf, environ[0], '=', strlen(environ[0]))))
        {
            *(p - 1) = 0;
            unsetenv(buf);
        }
    }

    create_proc(conf->NUM_PROC);
    fprintf(stdout, "   pid=%u, uid=%u, gid=%u\n", pid, getuid(), getgid());
    fprintf(stderr, "   pid=%u, uid=%u, gid=%u\n", pid, getuid(), getgid());

    if (signal(SIGSEGV, sig_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGSEGV): %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGINT): %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    for (int i = 0; i < conf->NUM_PROC; ++i)
        numConn[i] = 0;
    //--------------------------------------------------------------
    if (qu_init())
    {
        return -1;
    }
    
    pthread_t thr_send_sock;
    int n = pthread_create(&thr_send_sock, NULL, thread_send_socket, NULL);
    if (n)
    {
        print_err("<%s:%d> Error pthread_create(send_fd_sock): %s\n", __func__, __LINE__, strerror(n));
        exit(1);
    }
    //------------------------------------------------------------------
    static struct pollfd fdrd[3];

    fdrd[0].fd = from_chld[0];
    fdrd[0].events = POLLIN;

    fdrd[1].fd = sock_sig;
    fdrd[1].events = POLLIN;

    fdrd[2].fd = sockServer;
    fdrd[2].events = POLLIN;

    int num_fdrd = 3;

    while (run)
    {
        if (received_sig)
        {
            if (all_conn == 0)
            {
                if (received_sig == RESTART_SIG_)
                    run = 1;
                else if (received_sig == CLOSE_SIG_)
                    run = 0;
                else
                {
                    fprintf(stderr, "<%s:%d> Error: received_sig=%d\n", __func__, __LINE__, received_sig);
                    run = 1;
                }

                received_sig = 0;
                break;
            }
            else
                num_fdrd = 1;
        }

        int ret_poll = poll(fdrd, num_fdrd, -1);
        if (ret_poll <= 0)
        {
            print_err("<%s:%d> Error poll()=-1: %s\n", __func__, __LINE__, strerror(errno));
            continue;
        }

        if (fdrd[0].revents == POLLIN)
        {
            unsigned char s[8];
            int ret = read(from_chld[0], s, sizeof(s));
            if (ret <= 0)
            {
                print_err("<%s:%d> Error read()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                run = 0;
                break;
            }

            set_arr_conn(ret, s);
            all_conn  -= ret;
            ret_poll--;
        }

        if (ret_poll && (fdrd[1].revents == POLLIN))
        {
            int ret = read(sock_sig, &received_sig, sizeof(received_sig));
            if (ret <= 0)
            {
                print_err("<%s:%d> Error read()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                run = 0;
                break;
            }
            ret_poll--;
        }

        if (ret_poll && (fdrd[2].revents == POLLIN))
        {
            if (qu_not_full())
            {
                int clientSock = accept(sockServer, NULL, NULL);
                if (clientSock == -1)
                {
                    print_err("<%s:%d> Error accept()=-1: %s\n", __func__, __LINE__, strerror(errno));
                    run = 0;
                    break;
                }
                all_conn++;
                qu_push(clientSock);
            }
            ret_poll--;
        }

        if (ret_poll)
        {
            print_err("<%s:%d> fdrd[0].revents=0x%02x; fdrd[1].revents=0x%02x; ret=%d\n", __func__, __LINE__, 
                            fdrd[0].revents, fdrd[1].revents, ret_poll);
            run = 0;
            break;
        }
    }

    for (int i = 0; i < conf->NUM_PROC; ++i)
    {
        char ch = i;
        int ret = send_fd(unixFD[i], -1, &ch, 1);
        if (ret < 0)
        {
            fprintf(stderr, "<%s:%d> Error send_fd()\n", __func__, __LINE__);
            if (kill(pidArr[i], SIGKILL))
            {
                fprintf(stderr, "<%s:%d> Error: kill(%u, %u)\n", __func__, __LINE__, pidArr[i], SIGKILL);
            }
        }
        close(unixFD[i]);
    }

    if (run == 0)
    {
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);
        remove(pidFile);
        close(sock_sig);
        remove(name_sig_sock);
    }
    
    close(from_chld[0]);
    
    close_queue_sock();
    pthread_join(thr_send_sock, NULL);

    while ((pid = wait(NULL)) != -1)
    {
        fprintf(stderr, "<%d> wait() pid: %d\n", __LINE__, pid);
    }

    free_fcgi_list();
    if (run)
        fprintf(stderr, "<%s> ***** Reload, All connect: %u *****\n", __func__, all_conn);
    else
        fprintf(stderr, "<%s> ***** Close, All connect: %u *****\n", __func__, all_conn);
    close_logs();
    return 0;
}
//======================================================================
void manager(int sock, int num, int);
//======================================================================
pid_t create_child(int num_chld, int *from_chld)
{
    pid_t pid;

    errno = 0;
    pid = fork();

    if (pid == 0)
    {
        uid_t uid = getuid();
        if (uid == 0)
        {
            if (setgid(conf->server_gid) == -1)
            {
                fprintf(stderr, "<%s> Error setgid(%d): %s\n", __func__, conf->server_gid, strerror(errno));
                exit(1);
            }

            if (setuid(conf->server_gid) == -1)
            {
                fprintf(stderr, "<%s> Error setuid(%d): %s\n", __func__, conf->server_gid, strerror(errno));
                exit(1);
            }
        }

        close(from_chld[0]);
        manager(sockServer, num_chld, from_chld[1]);
        close(from_chld[1]);

        close_logs();
        exit(0);
    }
    else if (pid < 0)
    {
        fprintf(stderr, "<> Error fork(): %s\n", strerror(errno));
    }

    return pid;
}
//======================================================================
int seng_fd_timeout(int unix_sock, int fd, struct sockaddr_storage *addr, socklen_t size, int timeout)
{
    struct pollfd fdwr;

    fdwr.fd = fd;
    fdwr.events = POLLOUT;

    int ret = poll(&fdwr, 1, timeout * 1000);
    if (ret == -1)
    {
        print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (!ret)
    {
        print_err("<%s:%d> TimeOut poll(), tm=%d\n", __func__, __LINE__, timeout);
        return -1;
    }

    return send_fd(unix_sock, fd, addr, size);
}
//======================================================================
static int *buf_queue_sock = NULL;   // buffer queue sockets
static int size_qu, i_push, i_pop, thr_queue_close;

static pthread_mutex_t mtx_sock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_push_sock = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_close_conn = PTHREAD_COND_INITIALIZER;
//----------------------------------------------------------------------
static void close_queue_sock()
{
    thr_queue_close = 1;
    pthread_cond_signal(&cond_push_sock);
}
//----------------------------------------------------------------------
static int qu_init()
{
    size_qu = i_push = i_pop = thr_queue_close = 0;

    buf_queue_sock = malloc(conf->SIZE_QUEUE_CONNECT * sizeof(int));
    if (!buf_queue_sock)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    return 0;
}
//----------------------------------------------------------------------
int qu_not_full()
{
pthread_mutex_lock(&mtx_sock);
    int ret = conf->SIZE_QUEUE_CONNECT - size_qu;
pthread_mutex_unlock(&mtx_sock);
    return ret;
}
//----------------------------------------------------------------------
static void qu_push(int s)
{
pthread_mutex_lock(&mtx_sock);
    buf_queue_sock[i_push++] = s;
    if (i_push >= conf->SIZE_QUEUE_CONNECT)
        i_push = 0;
    size_qu++;
pthread_mutex_unlock(&mtx_sock);
    pthread_cond_signal(&cond_push_sock);
}
//----------------------------------------------------------------------
static void set_arr_conn(int n, const unsigned char *s)
{
pthread_mutex_lock(&mtx_sock);
    for ( int i = 0; i < n; i++)
    {
        numConn[s[i]]--;
    }
pthread_mutex_unlock(&mtx_sock);
    pthread_cond_signal(&cond_close_conn);
}
//----------------------------------------------------------------------
void timedwait_close_connect()
{
pthread_mutex_lock(&mtx_sock);
    struct timeval now;
    struct timespec ts;
    
    gettimeofday(&now, NULL);
    ts.tv_sec = now.tv_sec;
    ts.tv_nsec = (now.tv_usec + 1000) * 1000;
    
    pthread_cond_timedwait(&cond_close_conn, &mtx_sock, &ts);
    
pthread_mutex_unlock(&mtx_sock);
}
//----------------------------------------------------------------------
void *thread_send_socket(void *arg)
{
    int sock, i;
    char data[1] = "";

    while (1)
    {
    pthread_mutex_lock(&mtx_sock);
        while (size_qu == 0)
        {
            pthread_cond_wait(&cond_push_sock, &mtx_sock);
            if (thr_queue_close)
                break;
        }
        sock = buf_queue_sock[i_pop];
        
        for ( i = 0; thr_queue_close == 0; )
        {
            if (numConn[i] < conf->MAX_WORK_CONNECT)
                break;
            ++i;
            if (i >= conf->NUM_PROC)
            {
                pthread_cond_wait(&cond_close_conn, &mtx_sock);
                if (thr_queue_close)
                    break;
                i = 0;
            }
        }
    pthread_mutex_unlock(&mtx_sock);
 
        if (thr_queue_close)
        {
            free(buf_queue_sock);
            buf_queue_sock = NULL;
            return NULL;
        }
        //--------------------------------------------------------------
        int ret = send_fd(unixFD[i], sock, data, sizeof(data));
        if (ret == -ENOBUFS)
        {
            //print_err("<%s:%d> Error send_fd(%d), ENOBUFS\n", __func__, __LINE__, sock);
            timedwait_close_connect();
            continue;
        }
        else if (ret < 0)
        {
            print_err("<%s:%d> Error send_fd(%d), i=%d\n", __func__, __LINE__, sock, i_pop);
            exit(1);
        }

        close(sock);
        
    pthread_mutex_lock(&mtx_sock);
        numConn[i]++;
        size_qu--;
    pthread_mutex_unlock(&mtx_sock);
        //--------------------------------------------------------------
        i_pop++;
        if (i_pop >= conf->SIZE_QUEUE_CONNECT)
            i_pop = 0;
    }
    return NULL;
}
