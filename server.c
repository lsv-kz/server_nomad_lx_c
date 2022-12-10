#include "server.h"

static int sockServer;

int read_conf_file(const char *path_conf);
int create_server_socket(const Config *conf);
int get_sock_buf(int domain, int optname, int type, int protocol);
void create_logfiles(const char *log_dir);

int set_uid();
int main_proc();

static int from_chld[2], unixFD[8][2];
static pid_t pidArr[8];
static int numConn[8];
static char conf_dir[512];
static int start = 0, restart = 1;
static int close_chld_proc = 0;

static unsigned int all_conn = 0;

static char pidFile[MAX_PATH];
//======================================================================
static void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        fprintf(stderr, "<%s> ###### SIGINT ######\n", __func__);
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);
        close_chld_proc = 1;
    }
    else if (sig == SIGSEGV)
    {
        fprintf(stderr, "<%s> ###### SIGSEGV ######\n", __func__);
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);

        for (unsigned int i = 0; i < conf->NumProc; ++i)
        {
            if (kill(pidArr[i], SIGKILL) < 0)
                fprintf(stderr, "<%s> Error kill(): %s\n", __func__, strerror(errno));
        }

        pid_t pid;
        while ((pid = wait(NULL)) != -1)
        {
            fprintf(stderr, "<%s> wait() pid: %d\n", __func__, pid);
        }
        exit(1);
    }
    else if (sig == SIGTERM)
    {
        print_err("<main> ###### SIGTERM ######\n");
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);

        for (unsigned int i = 0; i < conf->NumProc; ++i)
        {
            if (kill(pidArr[i], SIGKILL) < 0)
                fprintf(stderr, "<%s> Error kill(): %s\n", __func__, strerror(errno));
        }

        pid_t pid;
        while ((pid = wait(NULL)) != -1)
        {
            fprintf(stderr, "<%s> wait() pid: %d\n", __func__, pid);
        }
        exit(0);
    }
    else if (sig == SIGUSR1)
    {
        fprintf(stderr, "<%s> ###### SIGUSR1 ######\n", __func__);
        restart = 1;
        close_chld_proc = 1;
    }
    else if (sig == SIGUSR2)
    {
        fprintf(stderr, "<%s> ###### SIGUSR2 ######\n", __func__);
        close_chld_proc = 1;
    }
    else
    {
        fprintf(stderr, "<%s> ? sig=%d\n", __func__, sig);
    }
}
//======================================================================
pid_t create_child(int, int *, int);
//======================================================================
void create_proc(int NumProc)
{
    if (pipe(from_chld) < 0)
    {
        fprintf(stderr, "<%s:%d> Error pipe(): %s\n", __func__, __LINE__, strerror(errno));
        exit(1);
    }
    //------------------------------------------------------------------
    int sndbuf = get_sock_buf(AF_UNIX, SO_SNDBUF, SOCK_DGRAM, 0);
    if (sndbuf < 0)
    {
        fprintf(stderr, " Error get_sock_buf(AF_UNIX, SOCK_DGRAM, 0): %s\n\n", strerror(-sndbuf));
        sndbuf = 0;
    }
    else
        fprintf(stderr, " AF_UNIX: SO_SNDBUF=%d\n\n", sndbuf);

    if (sndbuf >= 163840)
        sndbuf = 0;
    else
        sndbuf = 163840;

    pid_t pid_child;
    int i = 0;
    while (i < NumProc)
    {
        pid_child = create_child(i, from_chld, sndbuf);
        if (pid_child < 0)
        {
            fprintf(stderr, "<%s:%d> Error create_child() %d\n", __func__, __LINE__, i);
            exit(1);
        }
        pidArr[i] = pid_child;
        ++i;
    }

    close(from_chld[1]);
}
//======================================================================
void print_help(const char *name)
{
    fprintf(stderr, "Usage: %s [-l] [-c configfile] [-s signal]\n"
                    "Options:\n"
                    "   -h              : help\n"
                    "   -p              : print parameters\n"
                    "   -c configfile   : default: \"./server.conf\"\n"
                    "   -s signal       : restart, close, abort\n", name);
}
//======================================================================
void print_limits()
{
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
        fprintf(stderr, " Error getrlimit(RLIMIT_NOFILE): %s\n", strerror(errno));
    else
        fprintf(stderr, " RLIMIT_NOFILE: cur=%ld, max=%ld\n", (long)lim.rlim_cur, (long)lim.rlim_max);
    fprintf(stderr, " SC_NPROCESSORS_ONLN: %ld\n\n", sysconf(_SC_NPROCESSORS_ONLN));
    //------------------------------------------------------------------
    int sbuf = get_sock_buf(AF_INET, SO_SNDBUF, SOCK_STREAM, 0);
    if (sbuf < 0)
        fprintf(stderr, " Error get_sock_buf(AF_INET, SOCK_STREAM, 0): %s\n", strerror(-sbuf));
    else
        fprintf(stderr, " AF_INET: SO_SNDBUF=%d\n", sbuf);

    sbuf = get_sock_buf(AF_INET, SO_RCVBUF, SOCK_STREAM, 0);
    if (sbuf < 0)
        fprintf(stderr, " Error get_sock_buf(AF_INET, SOCK_STREAM, 0): %s\n", strerror(-sbuf));
    else
        fprintf(stderr, " AF_INET: SO_RCVBUF=%d\n\n", sbuf);
    //------------------------------------------------------------------
    sbuf = get_sock_buf(AF_UNIX, SO_SNDBUF, SOCK_DGRAM, 0);
    if (sbuf < 0)
        fprintf(stderr, " Error get_sock_buf(AF_UNIX, SOCK_DGRAM, 0): %s\n\n", strerror(-sbuf));
    else
        fprintf(stderr, " AF_UNIX: SO_SNDBUF=%d\n", sbuf);

    sbuf = get_sock_buf(AF_UNIX, SO_RCVBUF, SOCK_DGRAM, 0);
    if (sbuf < 0)
        fprintf(stderr, " Error get_sock_buf(AF_UNIX, SOCK_DGRAM, 0): %s\n\n", strerror(-sbuf));
    else
        fprintf(stderr, " AF_UNIX: SO_RCVBUF=%d\n\n", sbuf);
}
//======================================================================
void print_config()
{
    print_limits();
    printf(     "   ServerSoftware server        : \"%s\"\n"
                "   ServerAddr                   : %s\n"
                "   ServerPort                   : %s\n\n"

                "   ListenBacklog                : %d\n"
                "   TcpCork                      : %c\n"
                "   TcpNoDelay                   : %c\n"

                "   SendFile                     : %c\n"
                "   SndBufSize                   : %d\n\n"

                "   NumCpuCores                  : %d\n"

                "   MaxWorkConnections           : %d\n"
                "   MaxEventConnections          : %d\n\n"

                "   NumProc                      : %d\n"
                "   MaxThreads                   : %d\n"
                "   MimThreads                   : %d\n"
                "   MaxCgiProc                   : %d\n\n"

                "   TimeoutKeepAlive             : %d\n"
                "   TimeOut                      : %d\n"
                "   TimeOutCGI                   : %d\n"
                "   TimeoutPoll                  : %d\n\n"

                "   MaxRanges                    : %d\n\n"

                "   php                          : %s\n"
                "   path_php                     : %s\n\n"

                "   ShowMediaFiles               : %c\n"
                "   ClientMaxBodySize            : %ld\n\n"

                "   index.html                   : %c\n"
                "   index.php                    : %c\n"
                "   index.pl                     : %c\n"
                "   index.fcgi                   : %c\n",
                conf->ServerSoftware, conf->ServerAddr, conf->ServerPort, conf->ListenBacklog, conf->TcpCork, conf->TcpNoDelay,
                conf->SendFile, conf->SndBufSize, conf->NumCpuCores,
                conf->MaxWorkConnections, conf->MaxEventConnections, conf->NumProc, conf->MaxThreads, conf->MinThreads, conf->MaxCgiProc,
                conf->TimeoutKeepAlive, conf->Timeout, conf->TimeoutCGI, conf->TimeoutPoll,
                conf->MaxRanges, conf->UsePHP, conf->PathPHP, conf->ShowMediaFiles, conf->ClientMaxBodySize,
                conf->index_html, conf->index_php, conf->index_pl, conf->index_fcgi);
    printf("   %s\n   %s\n\n", conf->DocumentRoot, conf->ScriptPath);

    fcgi_list_addr *i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        fprintf(stdout, "   [%s] : [%s]\n", i->script_name, i->addr);
    }
}
//======================================================================
int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc == 1)
        snprintf(conf_dir, sizeof(conf_dir), "%s", "server.conf");
    else
    {
        int c, arg_print = 0;
        pid_t pid_ = 0;
        char *sig = NULL, *conf_dir_ = NULL;
        while ((c = getopt(argc, argv, "c:s:h:p")) != -1)
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
                    return 0;
                case 'p':
                    arg_print = 1;
                    break;
                default:
                    print_help(argv[0]);
                    return 0;
            }
        }

        if (conf_dir_)
            snprintf(conf_dir, sizeof(conf_dir), "%s", conf_dir_);
        else
            snprintf(conf_dir, sizeof(conf_dir), "%s", "server.conf");

        if (arg_print)
        {
            if (read_conf_file(conf_dir))
                return 1;
            print_config();
            return 0;
        }

        if (sig)
        {
            int sig_send;
            if (!strcmp(sig, "restart"))
                sig_send = SIGUSR1;
            else if (!strcmp(sig, "close"))
                sig_send = SIGUSR2;
            else if (!strcmp(sig, "abort"))
                sig_send = SIGTERM;
            else
            {
                fprintf(stderr, "<%d> ? option -s: %s\n", __LINE__, sig);
                print_help(argv[0]);
                return 1;
            }

            if (read_conf_file(conf_dir))
                return 1;
            snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->PidFilePath);
            FILE *fpid = fopen(pidFile, "r");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile, strerror(errno));
                return 1;
            }

            fscanf(fpid, "%u", &pid_);
            fclose(fpid);

            if (kill(pid_, sig_send))
            {
                fprintf(stderr, "<%d> Error kill(pid=%u, sig=%u): %s\n", __LINE__, pid_, sig_send, strerror(errno));
                return 1;
            }

            return 0;
        }
    }

    while (restart)
    {
        restart = 0;

        if (read_conf_file(conf_dir))
            return 1;

        set_uid();
        //--------------------------------------------------------------
        sockServer = create_server_socket(conf);
        if (sockServer == -1)
        {
            fprintf(stderr, "<%s:%d> Error: create_server_socket(%s:%s)\n", __func__, __LINE__, conf->ServerAddr, conf->ServerPort);
            exit(1);
        }
        //--------------------------------------------------------------
        if (start == 0)
        {
            start = 1;
            snprintf(pidFile, sizeof(pidFile), "%s/pid.txt", conf->PidFilePath);
            FILE *fpid = fopen(pidFile, "w");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile, strerror(errno));
                return 1;
            }

            fprintf(fpid, "%u\n", getpid());
            fclose(fpid);
            //----------------------------------------------------------
            if (signal(SIGINT, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s> Error signal(SIGINT): %s\n", __func__, strerror(errno));
                break;
            }

            if (signal(SIGTERM, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s> Error signal(SIGTERM): %s\n", __func__, strerror(errno));
                break;
            }

            if (signal(SIGSEGV, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s> Error signal(SIGSEGV): %s\n", __func__, strerror(errno));
                break;
            }

            if (signal(SIGUSR1, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s> Error signal(SIGUSR1): %s\n", __func__, strerror(errno));
                break;
            }

            if (signal(SIGUSR2, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s> Error signal(SIGUSR2): %s\n", __func__, strerror(errno));
                break;
            }
        }
        //--------------------------------------------------------------
        create_logfiles(conf->LogPath);
        //--------------------------------------------------------------
        int ret = main_proc();
        close_logs();
        if (ret)
            break;
    }

    if (start == 1)
        remove(pidFile);
    return 0;
}
//======================================================================
int main_proc()
{
    char s[256];
    //------------------------------------------------------------------
    pid_t pid = getpid();

    get_time(s, 64);
    printf("[%s] - server \"%s\" run, port: %s\n", s, conf->ServerSoftware, conf->ServerPort);

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
    fprintf(stdout, "  pid=%u, uid=%u, gid=%u\n\n", pid, getuid(), getgid());
    fprintf(stderr, "  pid=%u, uid=%u, gid=%u\n", pid, getuid(), getgid());
    fprintf(stderr, "  NumCpuCores: %u, MaxWorkConnections: %d\n"
                    "  SndBufSize: %d, MaxEventConnections: %d\n", conf->NumCpuCores, conf->MaxWorkConnections,
                             conf->SndBufSize, conf->MaxEventConnections);
    //------------------------------------------------------------------
    int sndbuf;
    socklen_t optlen = sizeof(sndbuf);
    getsockopt(unixFD[0][1], SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen);
    fprintf(stderr, "  <%s:%d> AF_UNIX: SO_SNDBUF=%d\n", __func__, __LINE__, sndbuf);
    //------------------------------------------------------------------
    for (int i = 0; i < conf->NumProc; ++i)
        numConn[i] = 0;

    static struct pollfd fdrd[2];

    fdrd[0].fd = from_chld[0];
    fdrd[0].events = POLLIN;

    fdrd[1].fd = sockServer;
    fdrd[1].events = POLLIN;

    close_chld_proc = 0;

    unsigned int num_fdrd = 2, i_proc = 0;

    while (1)
    {
        if (close_chld_proc)
        {
            if (all_conn == 0)
                break;
            num_fdrd = 1;
        }
        else
        {
            if (conf->NumCpuCores == 1)
                i_proc = 0;
            else
            {
                i_proc++;
                if (i_proc >= conf->NumProc)
                    i_proc = 0;
            }

            for (unsigned int i = i_proc; ; )
            {
                if (numConn[i_proc] < conf->MaxWorkConnections)
                {
                    num_fdrd = 2;
                    break;
                }

                i_proc++;
                if (i_proc >= conf->NumProc)
                    i_proc = 0;

                if (i_proc == i)
                {
                    num_fdrd = 1;
                    break;
                }
            }

            if (all_conn == 0)
                num_fdrd = 2;
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
                break;
            }

            for (int i = 0; i < ret; i++)
            {
                numConn[s[i]]--;
                all_conn--;
            }

            ret_poll--;
        }

        if (ret_poll && (fdrd[1].revents == POLLIN))
        {
            int clientSock = accept(sockServer, NULL, NULL);
            if (clientSock == -1)
            {
                print_err("<%s:%d> Error accept()=-1: %s\n", __func__, __LINE__, strerror(errno));
                break;
            }

            char data[1] = "";

            int ret = send_fd(unixFD[i_proc][1], clientSock, data, sizeof(data));
            if (ret < 0)
            {
                if (ret == -ENOBUFS)
                    print_err("[%d]<%s:%d> Error send_fd: ENOBUFS\n", i_proc, __func__, __LINE__);
                else
                {
                    print_err("<%s:%d> Error send_fd()\n", __func__, __LINE__);
                    break;
                }
            }
            else
            {
                numConn[i_proc]++;
                all_conn++;
            }
            close(clientSock);
            ret_poll--;
        }

        if (ret_poll)
        {
            print_err("<%s:%d> fdrd[0].revents=0x%02x; fdrd[1].revents=0x%02x\n", __func__, __LINE__,
                            fdrd[0].revents, fdrd[1].revents);
            break;
        }
    }

    for (unsigned int i = 0; i < conf->NumProc; ++i)
    {
        char ch = i;
        int ret = send_fd(unixFD[i][1], -1, &ch, 1);
        if (ret < 0)
        {
            fprintf(stderr, "<%s:%d> Error send_fd()\n", __func__, __LINE__);
            if (kill(pidArr[i], SIGKILL))
            {
                fprintf(stderr, "<%s:%d> Error: kill(%u, %u)\n", __func__, __LINE__, pidArr[i], SIGKILL);
            }
        }
        close(unixFD[i][1]);
    }

    close(sockServer);
    close(from_chld[0]);
    free_fcgi_list();

    while ((pid = wait(NULL)) != -1)
    {
        fprintf(stderr, "<%s> wait() pid: %d\n", __func__, pid);
    }

    if (restart == 0)
        fprintf(stderr, "<%s> ***** Close *****\n", __func__);
    else
        fprintf(stderr, "<%s> ***** Reload *****\n", __func__);

    fprintf(stderr, "<%s> ***** All connect: %u *****\n", __func__, all_conn);

    return 0;
}
//======================================================================
void manager(int, int, int, int);
//======================================================================
pid_t create_child(int num_chld, int *from_chld, int sock_buf)
{
    pid_t pid;
    int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, unixFD[num_chld]);
    if (ret < 0)
    {
        fprintf(stderr, "<%s:%d> Error socketpair(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    if (sock_buf > 0)
    {
        socklen_t optlen = sizeof(sock_buf);

        if (setsockopt(unixFD[num_chld][1], SOL_SOCKET, SO_SNDBUF, &sock_buf, optlen) < 0)
        {
            fprintf(stderr, "<%s:%d> Error setsockopt(SO_SNDBUF): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        if (setsockopt(unixFD[num_chld][0], SOL_SOCKET, SO_RCVBUF, &sock_buf, optlen) < 0)
        {
            fprintf(stderr, "<%s:%d> Error setsockopt(SO_RCVBUF): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }

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

        for (int i = 0; i <= num_chld; ++i)
        {
            close(unixFD[i][1]);
            //printf("[%d]<%s:%d> close[%d][1]=%d\n", num_chld, __func__, __LINE__, i, unixFD[i][0]);
        }

        close(from_chld[0]);
        manager(sockServer, num_chld, unixFD[num_chld][0], from_chld[1]);
        close(from_chld[1]);
        close(unixFD[num_chld][0]);

        close_logs();
        exit(0);
    }
    else if (pid < 0)
    {
        fprintf(stderr, "<> Error fork(): %s\n", strerror(errno));
    }

    close(unixFD[num_chld][0]);
    return pid;
}
