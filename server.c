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
static int get_sig = 0;
enum { RESTART_SIG = 1, CLOSE_SIG,};
//======================================================================
static void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        fprintf(stderr, "<main> ######  SIGINT  ######\n");
        get_sig = CLOSE_SIG;
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
    for (int i = 0; i < conf->NumProc; ++i)
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
            
            snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->pidDir);
            FILE *fpid = fopen(pidFile, "r");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile, strerror(errno));
                exit(1);
            }

            fprintf(fpid, "%u\n", getpid());
            fscanf(fpid, "%u", &pid_);
            fclose(fpid);

            int data;
            if (!strcmp(sig, "restart"))
                data = RESTART_SIG;
            else if (!strcmp(sig, "close"))
                data = CLOSE_SIG;
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

        snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->pidDir);
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
                fprintf(stderr, "<%s:%d> Error: create_server_socket(%s:%s)\n", __func__, __LINE__, conf->host, conf->servPort);
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

    create_logfiles(conf->logDir, conf->ServerSoftware);
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

    if (signal(SIGSEGV, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGSEGV): %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }

    if (signal(SIGINT, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGINT): %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    for (int i = 0; i < conf->NumProc; ++i)
        numConn[i] = 0;

    static struct pollfd fdrd[3];

    fdrd[0].fd = from_chld[0];
    fdrd[0].events = POLLIN;

    fdrd[1].fd = sock_sig;
    fdrd[1].events = POLLIN;

    fdrd[2].fd = sockServer;
    fdrd[2].events = POLLIN;

    int num_fdrd, i_fd;
    struct
    {
        int sock;
        int num;
    } err_send_fd;

    err_send_fd.sock = 0;

    struct sockaddr_storage clientAddr;
    socklen_t addrSize = sizeof(struct sockaddr_storage);

    while (run)
    {
        if (err_send_fd.sock > 0)
        {
            int ret = send_fd(unixFD[err_send_fd.num], err_send_fd.sock, &clientAddr, addrSize);
            if (ret == -1)
            {
                run = 0;
                break;
            }
            else if (ret == -ENOBUFS)
            {
                num_fdrd = 2;
                print_err("<%s:%d> Error send_fd(): ENOBUFS\n", __func__, __LINE__);
            }
            else
            {
                close(err_send_fd.sock);
                err_send_fd.sock = 0;
                numConn[err_send_fd.num]++;
            }
        }

        if (get_sig)
        {
            if (all_conn == 0)
            {
                if (get_sig == RESTART_SIG)
                    run = 1;
                else if (get_sig == CLOSE_SIG)
                    run = 0;
                else
                {
                    fprintf(stderr, "<%s:%d> Error: get_sig=%d\n", __func__, __LINE__, get_sig);
                    run = 1;
                }

                get_sig = 0;
                break;
            }
            else
                num_fdrd = 1;
        }
        else if (err_send_fd.sock == 0)
        {
            for (i_fd = 0; i_fd < conf->NumProc; ++i_fd)
            {
                if (numConn[i_fd] < conf->MAX_REQUESTS)
                    break;
            }

            if (i_fd >= conf->NumProc)
                num_fdrd = 2;
            else
                num_fdrd = 3;
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

            for ( int i = 0; i < ret; i++)
            {
                numConn[s[i]]--;
                all_conn--;
            }
            ret_poll--;
        }

        if (ret_poll && (fdrd[1].revents == POLLIN))
        {
            int ret = read(sock_sig, &get_sig, sizeof(get_sig));
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
            addrSize = sizeof(struct sockaddr_storage);
            int clientSock = accept(sockServer, (struct sockaddr *)&clientAddr, &addrSize);
            if (clientSock == -1)
            {
                print_err("<%s:%d> Error accept()=-1: %s\n", __func__, __LINE__, strerror(errno));
                run = 0;
                break;
            }
            all_conn++;

            int ret = send_fd(unixFD[i_fd], clientSock, &clientAddr, addrSize);
            //int ret = seng_fd_timeout(unixFD[i_fd], clientSock, &clientAddr, addrSize, 5);
            if (ret == -1)
            {
                run = 0;
                break;
            }
            else if (ret == -ENOBUFS)
            {
                err_send_fd.sock = clientSock;
                err_send_fd.num = i_fd;
                print_err("<%s:%d> Error send_fd: ENOBUFS\n", __func__, __LINE__);
                continue;
            }
            else
            {
                close(clientSock);
                numConn[i_fd]++;
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

    for (int i = 0; i < conf->NumProc; ++i)
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
