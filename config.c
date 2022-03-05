#include "server.h"

static struct Config c;
const struct Config* const conf = &c;

void create_logfiles(const char *log_dir, const char * ServerSoftware);
//======================================================================
int check_path(char *path, int size)
{
    struct stat st;
    
    int ret = stat(path, &st);
    if (ret == -1)
    {
        fprintf(stderr, "<%s:%d> Error stat(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "<%s:%d> [%s] is not directory\n", __func__, __LINE__, path);
        return -1;
    }
    
    char path_[PATH_MAX] = "";
    if (!realpath(path, path_))
    {
        fprintf(stderr, "<%s:%d> Error realpath(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    
    snprintf(path, size, "%s", path_);

    return 0;
}
//======================================================================
int create_conf_file()
{
    FILE *f;

    f = fopen("server.conf", "w");
    if (!f)
    {
        perror("   Error create conf file");
        exit(1);
    }

    fprintf(f, "ServerAddr   %s\n", "0.0.0.0");
    fprintf(f, "Port         ?\n");
    fprintf(f, "ServerSoftware   ?\n");
    
    fprintf(f, "tcp_cork   n\n");
    
    fprintf(f, "SndBufSize   32768\n");
    fprintf(f, "TcpNoDelay   y\n");
    
    fprintf(f, "SendFile   n\n\n");

    fprintf(f, "DocumentRoot %s\n", "?");
    fprintf(f, "ScriptPath   %s\n", "?");
    fprintf(f, "LogPath      %s\n\n", "?");

    fprintf(f, "MaxRequestsPerThr 100\n\n");
    
    fprintf(f, "MaxSndFd   200\n");
    fprintf(f, "TimeoutPoll 10\n\n");
    
    fprintf(f, "ListenBacklog 128\n\n");
    
    fprintf(f, "MaxRequests 512\n\n");

    fprintf(f, "NumChld 4\n");
    fprintf(f, "MaxThreads 256\n");
    fprintf(f, "MinThreads 6\n\n");
    
    fprintf(f, "MaxChldsCgi 30\n\n");

    fprintf(f, "KeepAlive   y\n");
    fprintf(f, "TimeoutKeepAlive 30\n");
    fprintf(f, "TimeOut    60\n");
    fprintf(f, "TimeoutCGI 10\n\n");

    fprintf(f, "Chunked  y\n");
    fprintf(f, "ClientMaxBodySize 5000000\n\n");

    fprintf(f, "UsePHP     n\n");
    fprintf(f, "# UsePHP   /usr/bin/php-cgi\n");
    fprintf(f, "# PathPHP  127.0.0.1:9000  #  /run/php/php7.0-fpm.sock\n\n");
    
    fprintf(f, "index {\n"
                "\t#index.html\n"
                "}\n\n");
    
    fprintf(f, "fastcgi {\n"
                "\t#/test  127.0.0.1:9009\n"
                "}\n\n");

    fprintf(f, "ShowMediaFiles n\n\n");

    fprintf(f, "index.html n\n");
    fprintf(f, "index.php  n\n");
    fprintf(f, "index.pl   n\n\n");

    fprintf(f, "User nobody     # www-data\n");
    fprintf(f, "Group nogroup   # www-data\n\n");

    fclose(f);
    return 0;
}
//======================================================================
fcgi_list_addr *create_fcgi_list(char *s)
{
    fcgi_list_addr *tmp = malloc(sizeof(fcgi_list_addr));
    if (!tmp)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }
    
    tmp->next = NULL;
    
    if (sscanf(s, "%64s %64s", tmp->scrpt_name, tmp->addr) != 2)
    {
        printf("<%s:%d> Error read line: (num_param != 2) %s\n", __func__, __LINE__, s);
        exit(1);
    }
    
    return tmp;
}
//======================================================================
int read_conf_file(const char *path_conf)
{
    FILE *f;
    char s[4096], *p;
    fcgi_list_addr *prev = NULL;

    f = fopen(path_conf, "r");
    if (!f)
    {
        if (create_conf_file())
        {
            perror("   Error create conf file");
            exit(1);
        }
        
        fprintf(stderr, " Correct config file\n");
        exit(1);
    }

    while (fgets(s, sizeof(s), f))
    {
        if ((p = strpbrk(s, "\r\n"))) *p = 0;
        p = s;
        while ((*p == ' ') || (*p == '\t'))
            p++;

        if (*p == '#' || *p == 0)
            continue;
        else
        {
            if (strchr(s, '#'))
                *strchr(s, '#') = 0;
        }

        if (sscanf(p, "ServerAddr %127s", c.host) == 1)
            continue;
        else if (sscanf(p, "Port %15s", c.servPort) == 1)
            continue;
        else if (sscanf(p, "ServerSoftware %63s", c.ServerSoftware) == 1)
            continue;
        else if (sscanf(p, "tcp_cork %c", &c.tcp_cork) == 1)
            continue;
        else if (sscanf(p, "SndBufSize %d", &c.SNDBUF_SIZE) == 1)
            continue;
        else if (sscanf(p, "SendFile %c", &c.SEND_FILE) == 1)
            continue;
        else if (sscanf(p, "TcpNoDelay %c", &c.TcpNoDelay) == 1)
            continue;
        else if (sscanf(p, "DocumentRoot %4000s", c.rootDir) == 1)
            continue;
        else if (sscanf(p, "ScriptPath %4000s", c.cgiDir) == 1)
            continue;
        else if (sscanf(p, "LogPath %4000s", c.logDir) == 1)
            continue;
        else if (sscanf(p, "MaxRequestsPerThr %d", &c.MaxRequestsPerThr) == 1)
            continue;
        else if (sscanf(p, "ListenBacklog %d", &c.ListenBacklog) == 1)
            continue;
        else if (sscanf(p, "MaxSndFd %d", &c.MAX_SND_FD) == 1)
            continue;
        else if (sscanf(p, "TimeoutPoll %d", &c.TIMEOUT_POLL) == 1)
            continue;
        else if (sscanf(p, "MaxRequests %d", &c.MAX_REQUESTS) == 1)
            continue;
        else if (sscanf(p, "NumChld %d", &c.NumChld) == 1)
            continue;
        else if (sscanf(p, "MaxThreads %d", &c.MaxThreads) == 1)
            continue;
        else if (sscanf(p, "MinThreads %d", &c.MinThreads) == 1)
            continue;
        else if (sscanf(p, "MaxChldsCgi %d", &c.MaxChldsCgi) == 1)
            continue;
        else if (sscanf(p, "KeepAlive%*[\x9\x20]%c", &c.KeepAlive) == 1)
            continue;
        else if (sscanf(p, "TimeoutKeepAlive %d", &c.TimeoutKeepAlive) == 1)
            continue;
        else if (sscanf(p, "TimeOut %d", &c.TimeOut) == 1)
            continue;
        else if (sscanf(p, "TimeoutCGI %d", &c.TimeoutCGI) == 1)
            continue;
        else if (sscanf(p, "UsePHP %15s", c.UsePHP) == 1)
            continue;
        else if (sscanf(p, "PathPHP %4000s", c.PathPHP) == 1)
            continue;
        else if (sscanf(p, "ShowMediaFiles %c", &c.ShowMediaFiles) == 1)
            continue;
        else if (sscanf(p, "Chunked %c", &c.Chunked) == 1)
            continue;
        else if (sscanf(p, "ClientMaxBodySize %ld", &c.ClientMaxBodySize) == 1)
            continue;
        else if (sscanf(p, "User %31s", c.user) == 1)
            continue;
        else if (sscanf(p, "Group %31s", c.group) == 1)
            continue;
        else if (!strncmp(p, "index", 5))
        {
            p += 5;
            while ((*p == ' ') || (*p == '\t'))
                p++;
            if (!strchr(p, '{'))
            {
                while (fgets(s, sizeof(s), f))
                {
                    if ((s[0] == '{') || (s[0] == '}'))
                        break;
                }
                
                if (s[0] != '{')
                {
                    printf("Error read_conf_file(): \"index\" [not found \'{\']\n");
                    exit(1);
                }
            }
            
            while (fgets(s, sizeof(s), f))
            {
                if ((p = strpbrk(s, "\r\n"))) *p = 0;
                p = s;
                while ((*p == ' ') || (*p == '\t'))
                    p++;

                if (*p == '#' || *p == 0)
                    continue;
                else if (*p == '}')
                    break;
                else if (strchr(p, '{'))
                    break;
                else
                {
                    if (strchr(s, '#'))
                        *strchr(s, '#') = 0;
                }
            printf(" > %s\n", p);
                if (!strncmp(p, "index.html", 10))
                    c.index_html = 'y';
                else if (!strncmp(p, "index.php", 9))
                    c.index_php = 'y';
                else if (!strncmp(p, "index.pl", 8))
                    c.index_pl = 'y';
                else if (!strncmp(p, "index.fcgi", 10))
                    c.index_fcgi = 'y';
            }
            
            if (*p != '}')
            {
                printf("Error read_conf_file(): \"index\" [not found \'}\']\n");
                exit(1);
            }
        }
        else if (!strncmp(p, "fastcgi", 7))
        {
            p += 7;
            while ((*p == ' ') || (*p == '\t'))
                p++;
            if (!strchr(p, '{'))
            {
                while (fgets(s, sizeof(s), f))
                {
                    if ((s[0] == '{') || (s[0] == '}'))
                        break;
                }
                
                if (s[0] != '{')
                {
                    printf("Error read_conf_file(): \"fastcgi\" [not found \'{\']\n");
                    exit(1);
                }
            }
            
            while (fgets(s,sizeof(s), f))
            {
                if ((p = strpbrk(s, "\r\n"))) *p = 0;
                p = s;
                while ((*p == ' ') || (*p == '\t'))
                    p++;

                if (*p == '#' || *p == '\0')
                    continue;
                else if (*p == '}')
                    break;
                else if (strchr(p, '{'))
                    break;
                else
                {
                    if (strchr(s, '#'))
                        *strchr(s, '#') = 0;
                }
            
                if (!prev)
                {
                    prev = c.fcgi_list = create_fcgi_list(p);
                }
                else
                {
                    fcgi_list_addr *tmp;
                    tmp = create_fcgi_list(p);
                    prev->next = tmp;
                    prev = tmp;
                }
            }
            
            if (*p != '}')
            {
                printf("Error read_conf_file(): \"fastcgi\" [not found \'}\']\n");
                free_fcgi_list();
                exit(1);
            }
        }
    }
    
    fclose(f);
    
    fcgi_list_addr *i = c.fcgi_list;
    for (; i; i = i->next)
    {
        printf("   [%s] : [%s]\n", i->scrpt_name, i->addr);
    }
    //------------------------------------------------------------------
    if (check_path(c.logDir, sizeof(c.logDir)) == -1)
    {
        fprintf(stderr, "!!! Error logDir [%s]\n", c.logDir);
        exit(1);
    }
    
    create_logfiles(c.logDir, c.ServerSoftware);
    //------------------------------------------------------------------
    if ((c.NumChld < 1) || (c.NumChld > 8))
    {
        fprintf(stderr, "<%s:%d> Error NumChld = %d; [1 < NumChld <= 6]\n", __func__, __LINE__, c.NumChld);
        exit(1);
    }
    
    if (c.MinThreads > c.MaxThreads)
    {
        fprintf(stderr, "<%s:%d> Error: NumThreads > MaxThreads\n", __func__, __LINE__);
        exit(1);
    }
    
    if (c.MinThreads < 1)
        c.MinThreads = 1;
    //------------------------------------------------------------------
    if (check_path(c.rootDir, sizeof(c.rootDir)) == -1)
    {
        fprintf(stderr, "!!! Error rootDir [%s]\n", c.rootDir);
        exit(1);
    }
    //------------------------------------------------------------------
    if (check_path(c.cgiDir, sizeof(c.cgiDir)) == -1)
    {
        c.cgiDir[0] = '\0';
        fprintf(stderr, "!!! Error cgi_dir [%s]\n", c.cgiDir);
    }
    
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
    {
        print_err("<%s:%d> Error getrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
    }
    else
    {
        printf("<%s:%d> lim.rlim_max=%lu, lim.rlim_cur=%lu\n", __func__, __LINE__, (unsigned long)lim.rlim_max, (unsigned long)lim.rlim_cur);
        long max_fd = (c.MAX_REQUESTS * 2) + 8;// stdin, stdout, stderr, flog, flog_err, socket(inet), pipe, soket(unix)
        if (max_fd > (long)lim.rlim_cur)
        {
            if (max_fd > (long)lim.rlim_max)
                lim.rlim_cur = lim.rlim_max;
            else
                lim.rlim_cur = max_fd;
            
            if (setrlimit(RLIMIT_NOFILE, &lim) == -1)
                print_err("<%s:%d> Error setrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
            max_fd = sysconf(_SC_OPEN_MAX);
            if (max_fd > 1)
            {
                print_err("<%s:%d> _SC_OPEN_MAX=%d\n", __func__, __LINE__, max_fd);
                c.MAX_REQUESTS = (max_fd - 8)/2;
                printf("<%s:%d> MaxRequests=%d, _SC_OPEN_MAX=%ld\n", __func__, __LINE__, c.MAX_REQUESTS, max_fd);
            }
            else
            {
                print_err("<%s:%d> Error sysconf(_SC_OPEN_MAX): %s\n", __func__, __LINE__, strerror(errno));
                close_logs();
                exit(1);
            }
        }
    }
    //------------------------------------------------------------------
    uid_t uid = getuid();
    if (uid == 0)
    {
        char *p;
        c.server_uid = strtol(c.user, &p, 0);
        if (*p == '\0')
        {
            struct passwd *passwdbuf = getpwuid(c.server_uid);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwuid(%u): %s\n", __func__, __LINE__, c.server_uid, strerror(errno));
                exit(1);
            }
        }
        else
        {
            struct passwd *passwdbuf = getpwnam(c.user);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwnam(%s): %s\n", __func__, __LINE__, c.user, strerror(errno));
                exit(1);
            }
            c.server_uid = passwdbuf->pw_uid;
        }
        
        c.server_gid = strtol(c.group, &p, 0);
        if (*p == '\0')
        {
            struct group *groupbuf = getgrgid(c.server_gid);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrgid(%u): %s\n", __func__, __LINE__, c.server_gid, strerror(errno));
                exit(1);
            }
        }
        else
        {
            struct group *groupbuf = getgrnam(c.group);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrnam(%s): %s\n", __func__, __LINE__, c.group, strerror(errno));
                exit(1);
            }
            c.server_gid = groupbuf->gr_gid;
        }
        //--------------------------------------------------------------
        if (c.server_uid != uid)
        {
            if (setuid(c.server_uid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setuid(%u): %s\n", __func__, __LINE__, c.server_uid, strerror(errno));
                exit(1);
            }
        }
    }
    else
    {
        c.server_uid = getuid();
        c.server_gid = getgid();
    }
    
    return 0;
}
//======================================================================
void free_fcgi_list()
{
    fcgi_list_addr *prev;
    while (c.fcgi_list)
    {
        prev = c.fcgi_list;
        c.fcgi_list = c.fcgi_list->next;
        if (prev) free(prev);
    }
}
//======================================================================
void set_sndbuf(int n)
{
    c.SNDBUF_SIZE = n;
}
