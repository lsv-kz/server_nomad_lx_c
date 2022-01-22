#include "server.h"

uid_t server_uid;
gid_t server_gid;

static int sockServer;

int create_server_socket(const struct Config *conf);
int read_conf_file(const char *path_conf);

static int from_chld[2], unixFD[8];
static int numConn[8];
//======================================================================
static void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        print_err("<main> ######  SIGINT  ######\n");
        /*for (int i = 0; i < conf->NumChld; ++i)
        {
            print_err("<main> numConn[%d]=%d\n", i, numConn[i]);
        }*/
    }
    else if (sig == SIGSEGV)
    {
        print_err("<main> ######  SIGSEGV  ######\n");
        exit(1);
    }
    else if (sig == SIGPIPE)
    {
        print_err("<main> ######  SIGPIPE  ######\n");
    }
    else
    {
        print_err("sig=%d\n", sig);
    }
}
//======================================================================
pid_t create_child(int num_chld, int *from_chld);
//======================================================================
void create_proc(int NumChld)
{
    if (pipe(from_chld) < 0)
    {
        printf("<%s():%d> Error pipe(): %s\n", __FUNCTION__, __LINE__, strerror(errno));
        exit(1);
    }
    //------------------------------------------------------------------
    pid_t pid_child;
    int numChld = 0;
    while (numChld < NumChld)
    {
        char s[32];
        snprintf(s, sizeof(s), "unix_sock_%d", numChld);
        
        pid_child = create_child(numChld, from_chld);
        if (pid_child < 0)
        {
            print_err("[%d]<%s:%d> Error create_child() %d\n", numChld, __func__, __LINE__, numChld);
            exit(1);
        }
        ++numChld;
    }
    
    close(from_chld[1]);
    sleep(1);
    numChld = 0;
    while (numChld < NumChld)
    {
        char s[32];
        snprintf(s, sizeof(s), "unix_sock_%d", numChld);
        
        if ((unixFD[numChld] = unixConnect(s)) < 0)
        {
            printf("[%d]<%s:%d> Error create_fcgi_socket(%s)=%d: %s\n", numChld, __func__, __LINE__, 
                            s, unixFD[numChld], strerror(errno));
            exit(1);
        }
        
        remove(s);
        ++numChld;
    }
}
//======================================================================
int main(int argc, char *argv[])
{
    pid_t pid;
    char s[256];

    if (argc == 1)
    {
        read_conf_file("./server.conf");
    }
    else
    {
        int n = strlen(argv[1]);
        if((sizeof(s) - 1) <= n)
        {
            fprintf(stderr, "   Error: path of config file is large\n");
            exit(EXIT_FAILURE);
        }
        memcpy(s, argv[1], n);
        s[n] = 0;
        if(s[n-1] != '/')
        {
            s[n] = '/';
            s[++n] = 0;
        }
        if((sizeof(s) - (n + 1)) <= strlen("server.conf"))
        {
            fprintf(stderr, "..   Error: path of config file is large\n");
            exit(EXIT_FAILURE);
        }

        n = strlen(s);
        memcpy(s + n, "server.conf", 12);
        
        read_conf_file(s);
    }

    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        fprintf(stderr, "   Error signal(SIGPIPE,)!\n");
        exit(EXIT_FAILURE);
    }

    pid = getpid();
    //------------------------------------------------------------------
    if ((conf->NumChld < 1) || (conf->NumChld > 8))
    {
        print_err("<%s:%d> Error NumChld = %d; [1 < NumChld <= 6]\n", __func__, __LINE__, conf->NumChld);
        exit(1);
    }
    
    sockServer = create_server_socket(conf);
    if(sockServer == -1)
    {
        fprintf(stderr, "<%d>   server: failed to bind\n", __LINE__);
        getchar();
        exit(1);
    }

    get_time(s, 64);
    printf(     " [%s] - server \"%s\" run\n"
                "   ip = %s;\n"
                "   port = %s;\n"
                "   tcp_cork = %c;\n"
                "   SndBufSize = %d;\n"
                "   SendFile = %c;\n"
                "   TcpNoDelay = %c\n\n"
                "   NumChld = %d\n"
                "   MaxThreads = %d\n"
                "   MimThreads = %d\n\n"
                "   MaxChldsCgi = %d\n"
                "   ListenBacklog = %d\n"
                "   MaxRequests = %d\n\n" 
                "   KeepAlive %c\n"
                "   TimeoutKeepAlive = %d\n"
                "   TimeOut = %d\n"
                "   TimeOutCGI = %d\n\n"
                "   php: %s\n"
                "   path_php: %s\n"
                "   ShowMediaFiles = %c\n"
                "   Chunked = %c\n"
                "   ClientMaxBodySize = %ld\n"
                "  ------------- pid = %d -----------\n",
                s, conf->ServerSoftware, conf->host, conf->servPort, conf->tcp_cork, conf->SNDBUF_SIZE, conf->SEND_FILE, conf->TcpNoDelay, conf->NumChld,
                conf->MaxThreads, conf->MinThreads, conf->MaxChldsCgi, conf->ListenBacklog, conf->MAX_REQUESTS,
                conf->KeepAlive, conf->TimeoutKeepAlive, conf->TimeOut, conf->TimeoutCGI, 
                conf->UsePHP, conf->PathPHP, 
                conf->ShowMediaFiles, conf->Chunked, conf->ClientMaxBodySize, pid);
    printf("   %s;\n   %s\n\n", conf->rootDir, conf->cgiDir);
    fprintf(stderr, "  uid=%u; gid=%u\n", getuid(), getgid());
    
    for ( ; environ[0]; )
    {
        char *p, buf[512];
        if ((p = (char*)memccpy(buf, environ[0], '=', strlen(environ[0]))))
        {
            *(p - 1) = 0;
            unsetenv(buf);
        }
    }
/*
    for (int n = 0; environ[n]; ++n)
    {
        printf(" %s\n", environ[n]);
    }
*/
    if(conf->MinThreads > conf->MaxThreads)
    {
        fprintf(stderr, "<%s():%d> Error: NumThreads > MaxThreads\n", __func__, __LINE__);
        exit(1);
    }
    
    create_proc(conf->NumChld);
    
    printf("   pid main proc: %d\n", pid);
    
    if(signal(SIGSEGV, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGSEGV,)!\n");
        exit (EXIT_FAILURE);
    }
    
    if(signal(SIGINT, signal_handler) == SIG_ERR)
    {
        fprintf (stderr, "   Error signal(SIGINT,)!\n");
        exit (EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    int close_server = 0;
    static struct pollfd fdrd[2];
    
    fdrd[0].fd = from_chld[0];
    fdrd[0].events = POLLIN;
    
    fdrd[1].fd = sockServer;
    fdrd[1].events = POLLIN;
    
    int num_fdrd, i_fd;
    
    while (!close_server)
    {
        int ret, clientSock;
        num_fdrd = 2;
        i_fd = -1;
        
        for (i_fd = 0; i_fd < conf->NumChld; ++i_fd)
        {
            if (numConn[i_fd] < conf->MAX_REQUESTS)
                break;
        }
        
        if (i_fd >= conf->NumChld)
            num_fdrd = 1;
        
        int ret_poll = poll(fdrd, num_fdrd, -1);
        if (ret_poll == -1)
        {
            print_err("<%s:%d> Error poll()=-1: %s\n", __func__, __LINE__, strerror(errno));
            break;
        }
        else if (ret_poll == 0)
        {
            print_err("<%s:%d> ???\n", __func__, __LINE__);
            break;
        }
        
        if (fdrd[0].revents == POLLIN)
        {
            unsigned char s[32];
            ret = read(from_chld[0], s, sizeof(s));
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
        
        if ((fdrd[1].revents == POLLIN) && (ret_poll))
        {
            struct sockaddr_storage clientAddr;
            socklen_t addrSize = sizeof(struct sockaddr_storage);// 128
            
            clientSock = accept(sockServer, (struct sockaddr *)&clientAddr, &addrSize);
            if (clientSock == -1)
            {
                print_err("<%s:%d> Error accept()=-1: %s\n", __func__, __LINE__, strerror(errno));
                break;
            }
            
            ret = send_fd(unixFD[i_fd], clientSock, &clientAddr, addrSize);
            if (ret < 0)
            {
                print_err("<%s:%d> Error sendClientSock()\n", __func__, __LINE__);
            }
            
            close(clientSock);
            numConn[i_fd]++;
            ret_poll--;
        }
        
        if (ret_poll)
        {
            print_err("<%s:%d> fdrd[0].revents=0x%02x; fdrd[1].revents=0x%02x; ret=%d\n", __func__, __LINE__, 
                            fdrd[0].revents, fdrd[1].revents, ret_poll);
            break;
        }
    }
    
    print_err("<%s:%d> ********************\n", __func__, __LINE__);
    for (int i = 0; i < conf->NumChld; ++i)
    {
        close(unixFD[i]);
    }
    
    int numChld = 0;
    while (numChld < conf->NumChld)
    {
        char s[32];
        snprintf(s, sizeof(s), "unix_sock_%d", numChld);
        
        remove(s);
        ++numChld;
    }
    //------------------------------------------------------------------
    close(from_chld[0]);
    close(sockServer);
    
    while ((pid = wait(NULL)) != -1)
    {
        print_err("<> wait() pid: %d\n", pid);
        continue;
    }
    
    free_fcgi_list();
    
    print_err("<%s:%d> Exit server\n", __func__, __LINE__);
    
    close_logs();
    sleep(1);
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
                perror("setgid");
                printf("<%s> Error setgid(%d): %s\n", __func__, conf->server_gid, strerror(errno));
                exit(1);
            }
            
            if (setuid(conf->server_gid) == -1)
            {
                perror("setuid");
                printf("<%s> Error setuid(%d): %s\n", __func__, conf->server_uid, strerror(errno));
                exit(1);
            }
        }

        for (int i = 0; i < num_chld; ++i)
        {
            close(unixFD[i]);
        }
        
        close(from_chld[0]);
        manager(sockServer, num_chld, from_chld[1]);
        close(from_chld[1]);
        
        close_logs();
        exit(0);
    }
    else if (pid < 0)
    {
        print_err("<> Error fork(): %s\n", strerror(errno));
    }
    
    return pid;
}
