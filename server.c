#include "server.h"

static int sockServer;

int create_server_socket(const Config *conf);
int read_conf_file(const char *path_conf);
void create_logfiles(const char *log_dir, const char * ServerSoftware);
int set_uid();
int main_proc();

static int from_chld[2], unixFD[8];
static pid_t pidArr[8];
static int numConn[8];
static char conf_dir[512];
static int restart = 0;
static unsigned int all_conn = 0;
//======================================================================
static void signal_handler(int sig)
{
    fprintf(stderr, "<main> All connect: %u\n", all_conn);
    if (sig == SIGINT)
    {
        fprintf(stderr, "<main> ######  SIGINT  ######\n");
    }
    else if (sig == SIGSEGV)
    {
        fprintf(stderr, "<main> ######  SIGSEGV  ######\n");
        exit(1);
    }
    else if (sig == SIGUSR1)
    {
        fprintf(stderr, "<main> ####### SIGUSR1 #######\n");
        char ch[1];
        for (int i = 0; i < conf->NumProc; ++i)
        {
            int ret = send_fd(unixFD[i], -1, ch, 1);
            if (ret < 0)
            {
                fprintf(stderr, "<%s:%d> Error sendClientSock()\n", __func__, __LINE__);
                if (kill(pidArr[i], SIGKILL))
                {
                    fprintf(stderr, "<%s:%d> Error: kill(%u, %u)\n", __func__, __LINE__, pidArr[i], SIGKILL);
                    exit(1);
                }
            }
        }

        restart = 1;
    }
    else
    {
        fprintf(stderr, "sig=%d\n", sig);
    }
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
        
        if ((unixFD[i] = unixConnect(s)) < 0)
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
    fprintf(stderr, "Usage: %s [-c configfile] [-p pid] [-s signal]\n   signals: restart\n", name);
    exit(EXIT_FAILURE);
}
//======================================================================
int main(int argc, char *argv[])
{
    signal(SIGUSR2, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    if (argc == 1)
        snprintf(conf_dir, sizeof(conf_dir), "%s", "./server.conf");
    else
    {
        int c;
        pid_t pid_ = 0;
        char *sig = NULL, *conf_dir_ = NULL, *p;
        while ((c = getopt(argc, argv, "c:p:s:h")) != -1)
        {
            switch (c)
            {
                case 'c':
                    conf_dir_ = optarg;
                    break;
                case 'p':
                    p = optarg;
                    if (sscanf(p, "%u", &pid_) != 1)
                    {
                        fprintf(stderr, "<%s:%d> Error: sscanf()\n", __func__, __LINE__);
                        print_help(argv[0]);
                        exit(0);
                    }
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

            if (pid_ && (!strcmp(sig, "restart")))
            {
                if (kill(pid_, SIGUSR1))
                {
                    fprintf(stderr, "<%d> Error kill(pid=%u, sig=%u): %s\n", __LINE__, pid_, SIGUSR1, strerror(errno));
                    exit(1);
                }
                exit(0);
            }
            else
            {
                fprintf(stderr, "<%d> ? pid=%u, %s\n", __LINE__, pid_, sig);
                print_help(argv[0]);
                exit(1);
            }
            return 0;
        }
    }

    while (!restart)
    {
        if (main_proc())
            break;

        if (restart == 1)
            restart = 0;
        else
            break;
    }

    return 0;
}
//======================================================================
static int run = 0;
//----------------------------------------------------------------------
int main_proc()
{
    char s[256];

    if (read_conf_file(conf_dir))
        return -1;
    sockServer = create_server_socket(conf);
    if (sockServer == -1)
    {
        fprintf(stderr, "<%d>   server: failed to bind [%s:%s]\n", __LINE__, conf->host, conf->servPort);
        exit(1);
    }

    create_logfiles(conf->logDir, conf->ServerSoftware);
    if (run == 0)
    {
        set_uid();
        run = 1;
    }
    //------------------------------------------------------------------
    pid_t pid = getpid();

    get_time(s, 64);
    printf(     " [%s] - server \"%s\" run\n"
                "   ip = %s\n"
                "   port = %s\n"
                "   tcp_cork = %c\n"
                "   TcpNoDelay = %c\n\n"
                "   SendFile = %c\n"
                "   SendFileSizePart = %ld\n"
                "   SndBufSize = %d\n"
                "   MaxSndFd = %d\n"
                "   TimeoutPoll = %d\n\n"
                "   NumChld = %d\n"
                "   MaxThreads = %d\n"
                "   MimThreads = %d\n\n"
                "   MaxProcCgi = %d\n"
                "   ListenBacklog = %d\n"
                "   MaxRequests = %d\n\n" 
                "   KeepAlive %c\n"
                "   TimeoutKeepAlive = %d\n"
                "   TimeOut = %d\n"
                "   TimeOutCGI = %d\n\n"
                "   MaxRanges = %d\n\n"
                "   php: %s\n"
                "   path_php: %s\n"
                "   ShowMediaFiles = %c\n"
                "   ClientMaxBodySize = %ld\n\n"
                "   index.html = %c\n"
                "   index.php = %c\n"
                "   index.pl = %c\n"
                "   index.fcgi = %c\n",
                s, conf->ServerSoftware, conf->host, conf->servPort, conf->tcp_cork, conf->TcpNoDelay,
                conf->SEND_FILE, conf->SEND_FILE_SIZE_PART, conf->SNDBUF_SIZE, conf->MAX_SND_FD,
                conf->TIMEOUT_POLL, conf->NumProc, conf->MaxThreads, conf->MinThreads, conf->MaxProcCgi,
                conf->ListenBacklog, conf->MAX_REQUESTS, conf->KeepAlive, conf->TimeoutKeepAlive, conf->TimeOut,
                conf->TimeoutCGI, conf->MaxRanges, conf->UsePHP, conf->PathPHP, conf->ShowMediaFiles,
                conf->ClientMaxBodySize, conf->index_html, conf->index_php, conf->index_pl, conf->index_fcgi);
    printf("   %s;\n   %s\n\n", conf->rootDir, conf->cgiDir);

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

    create_proc(conf->NumProc);
    fprintf(stdout, "   pid=%u, uid=%u, gid=%u\n", pid, getuid(), getgid());
    fprintf(stderr, "   pid=%u, uid=%u, gid=%u\n", pid, getuid(), getgid());
    
    if (signal(SIGUSR1, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "<%s:%d> Error signal(SIGUSR1): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGSEGV, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGSEGV)!\n");
        exit (EXIT_FAILURE);
    }

    if (signal(SIGINT, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGINT)!\n");
        exit (EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    for (int i = 0; i < conf->NumProc; ++i)
        numConn[i] = 0;

    int close_server = 0;
    static struct pollfd fdrd[2];

    fdrd[0].fd = from_chld[0];
    fdrd[0].events = POLLIN;

    fdrd[1].fd = sockServer;
    fdrd[1].events = POLLIN;

    int num_fdrd, i_fd;
    int send_sock = 0;

    struct sockaddr_storage clientAddr;
    socklen_t addrSize = sizeof(struct sockaddr_storage);

    while (!close_server)
    {
        for (i_fd = 0; i_fd < conf->NumProc; ++i_fd)
        {
            if (numConn[i_fd] < conf->MAX_REQUESTS)
                break;
        }

        if (i_fd >= conf->NumProc)
            num_fdrd = 1;
        else
            num_fdrd = 2;

        if (send_sock > 0)
        {
            int ret = send_fd(unixFD[i_fd], send_sock, &clientAddr, addrSize);
            if (ret == -ENOBUFS)
            {
                num_fdrd = 1;
                print_err("<%s:%d> Error send_fd: ENOBUFS\n", __func__, __LINE__);
            }
            else if (ret == -1)
            {
                print_err("<%s:%d> Error sendClientSock()\n", __func__, __LINE__);
                break;
            }
            else
            {
                close(send_sock);
                send_sock = 0;
                numConn[i_fd]++;
                continue;
            }
        }

        int ret_poll = poll(fdrd, num_fdrd, -1);
        if (ret_poll <= 0)
        {
            //if (errno == EINTR)
            //    continue;
            print_err("<%s:%d> Error poll()=-1: %s\n", __func__, __LINE__, strerror(errno));
            break;
        }

        if (fdrd[0].revents == POLLIN)
        {
            unsigned char s[8];
            int ret = read(from_chld[0], s, sizeof(s));
            if (ret <= 0)
            {
                print_err("<%s:%d> Error read()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                break;
            }

            for ( int i = 0; i < ret; i++)
            {
                numConn[s[i]]--;
            }
            ret_poll--;
        }

        if (ret_poll && (fdrd[1].revents == POLLIN))
        {
            addrSize = sizeof(struct sockaddr_storage);
            int clientSock = accept(sockServer, (struct sockaddr *)&clientAddr, &addrSize);
            if (clientSock == -1)
            {
                print_err("<%s:%d> Error accept()=-1: %s\n", __func__, __LINE__, strerror(errno));
                break;
            }
            all_conn++;
            int ret = send_fd(unixFD[i_fd], clientSock, &clientAddr, addrSize);
            if (ret == -ENOBUFS)
            {
                send_sock = clientSock;
                print_err("<%s:%d> Error send_fd: ENOBUFS\n", __func__, __LINE__);
                continue;
            }
            else if (ret == -1)
            {
                print_err("<%s:%d> Error send_fd()\n", __func__, __LINE__);
                break;
            }
            else
            {
                close(clientSock);
                numConn[i_fd]++;
                ret_poll--;
            }
        }

        if (ret_poll)
        {
            print_err("<%s:%d> fdrd[0].revents=0x%02x; fdrd[1].revents=0x%02x; ret=%d\n", __func__, __LINE__, 
                            fdrd[0].revents, fdrd[1].revents, ret_poll);
            break;
        }
    }

    print_err("<%s:%d> ********************\n", __func__, __LINE__);
    for (int i = 0; i < conf->NumProc; ++i)
    {
        shutdown(unixFD[i], SHUT_RDWR);
        close(unixFD[i]);
    }

    shutdown(sockServer, SHUT_RDWR);
    close(sockServer);
    close(from_chld[0]);

    while ((pid = wait(NULL)) != -1)
    {
        fprintf(stderr, "<%d> wait() pid: %d\n", __LINE__, pid);
    }

    free_fcgi_list();
    print_err("<%s:%d> Exit server\n", __func__, __LINE__);
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
