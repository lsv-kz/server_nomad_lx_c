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

    fprintf(f, "NumProc 4\n");
    fprintf(f, "MaxThreads 256\n");
    fprintf(f, "MinThreads 6\n\n");

    fprintf(f, "MaxProcCgi 30\n\n");

    fprintf(f, "KeepAlive   y\n");
    fprintf(f, "TimeoutKeepAlive 30\n");
    fprintf(f, "TimeOut    60\n");
    fprintf(f, "TimeoutCGI 10\n\n");

    fprintf(f, "MaxRanges 0\n\n");

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
fcgi_list_addr *create_fcgi_list(const char *s1, const char *s2)
{
    fcgi_list_addr *tmp = malloc(sizeof(fcgi_list_addr));
    if (!tmp)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    tmp->next = NULL;
    tmp->scrpt_name = strdup(s1);
    tmp->addr = strdup(s2);
    
    if (!tmp->scrpt_name || !tmp->addr)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    return tmp;
}
//======================================================================
int rstrip(char *s, int n)
{
    while ((n > 0) && (((*(s + n - 1) == ' ') || (*(s + n - 1) == '\t'))))
        n--;
    *(s + n) = 0;
    return n;
}
//======================================================================
int getLine(FILE *f, char *s, int size)
{
    char *p = s;
    int ch, n = 0, wr = 1;
    *s = '\0';

    while (((ch = getc(f)) != EOF) && (n < size))
    {
        if ((char)ch == '\n')
        {
            *p = 0;
            if (n)
                return rstrip(s, n);
            else
            {
                wr = 1;
                p = s;
                *s = '\0';
                continue;
            }
        }

        if ((wr == 0) || (ch == '\r'))
            continue;

        switch (ch)
        {
            case ' ':
            case '\t':
                if (n)
                {
                    *(p++) = ' ';
                    ++n;
                }
                break;
            case '#':
                wr = 0;
                break;
            case '{':
            case '}':
                if (n)
                    fseek(f, -1, 1);
                else
                {
                    *(p++) = (char)ch;
                    ++n;
                }
                *p = 0;
                return rstrip(s, n);
            default:
                *(p++) = (char)ch;
                ++n;
        }
    }

    *p = 0;
    if (ch == EOF)
    {
        if (n)
            return n;
        else
            return -1;
    }

    return -1;
}
//======================================================================
int find_str(FILE *f, const char *s)
{
    int ch, i = 0, n = 0, len = strlen(s);

    while (((ch = getc(f)) != EOF))
    {
        n++;
        if (ch == *(s + i))
            i++;
        else
            i = 0;
        if (*(s + i) == 0)
            return n - len;
    }

    if (*(s + i) == 0)
        return n - len;
    return -1;
}
//======================================================================
int read_conf_file(const char *path_conf)
{
    FILE *f;
    char ss[512];
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

    int n;
    while ((n = getLine(f, ss, sizeof(ss))) >= 0)
    {
        if (sscanf(ss, "ServerAddr %127s", c.host) == 1)
            continue;
        else if (sscanf(ss, "Port %15s", c.servPort) == 1)
            continue;
        else if (sscanf(ss, "ServerSoftware %63s", c.ServerSoftware) == 1)
            continue;
        else if (sscanf(ss, "tcp_cork %c", &c.tcp_cork) == 1)
            continue;
        else if (sscanf(ss, "SndBufSize %d", &c.SNDBUF_SIZE) == 1)
            continue;
        else if (sscanf(ss, "SendFile %c", &c.SEND_FILE) == 1)
            continue;
        else if (sscanf(ss, "TcpNoDelay %c", &c.TcpNoDelay) == 1)
            continue;
        else if (sscanf(ss, "DocumentRoot %4000s", c.rootDir) == 1)
            continue;
        else if (sscanf(ss, "ScriptPath %4000s", c.cgiDir) == 1)
            continue;
        else if (sscanf(ss, "LogPath %4000s", c.logDir) == 1)
            continue;
        else if (sscanf(ss, "MaxRequestsPerThr %d", &c.MaxRequestsPerThr) == 1)
            continue;
        else if (sscanf(ss, "ListenBacklog %d", &c.ListenBacklog) == 1)
            continue;
        else if (sscanf(ss, "MaxSndFd %d", &c.MAX_SND_FD) == 1)
            continue;
        else if (sscanf(ss, "TimeoutPoll %d", &c.TIMEOUT_POLL) == 1)
            continue;
        else if (sscanf(ss, "MaxRequests %d", &c.MAX_REQUESTS) == 1)
            continue;
        else if (sscanf(ss, "NumProc %d", &c.NumProc) == 1)
            continue;
        else if (sscanf(ss, "MaxThreads %d", &c.MaxThreads) == 1)
            continue;
        else if (sscanf(ss, "MinThreads %d", &c.MinThreads) == 1)
            continue;
        else if (sscanf(ss, "MaxProcCgi %d", &c.MaxProcCgi) == 1)
            continue;
        else if (sscanf(ss, "KeepAlive%*[\x9\x20]%c", &c.KeepAlive) == 1)
            continue;
        else if (sscanf(ss, "TimeoutKeepAlive %d", &c.TimeoutKeepAlive) == 1)
            continue;
        else if (sscanf(ss, "TimeOut %d", &c.TimeOut) == 1)
            continue;
        else if (sscanf(ss, "TimeoutCGI %d", &c.TimeoutCGI) == 1)
            continue;
        else if (sscanf(ss, "MaxRanges %d", &c.MaxRanges) == 1)
            continue;
        else if (sscanf(ss, "UsePHP %15s", c.UsePHP) == 1)
            continue;
        else if (sscanf(ss, "PathPHP %4000s", c.PathPHP) == 1)
            continue;
        else if (sscanf(ss, "ShowMediaFiles %c", &c.ShowMediaFiles) == 1)
            continue;
        else if (sscanf(ss, "Chunked %c", &c.Chunked) == 1)
            continue;
        else if (sscanf(ss, "ClientMaxBodySize %ld", &c.ClientMaxBodySize) == 1)
            continue;
        else if (sscanf(ss, "User %31s", c.user) == 1)
            continue;
        else if (sscanf(ss, "Group %31s", c.group) == 1)
            continue;
        else if (!strcmp(ss, "index"))
        {
            if ((n = find_str(f, "{")) < 0)
            {
                fprintf(stderr, "<%s:%d>   Error read config file, n=%d\n", __func__, __LINE__, n);
                exit(1);
            }

            c.index_html = c.index_php = c.index_pl = c.index_fcgi = 'n';

            while ((n = getLine(f, ss, sizeof(ss))) >= 0)
            {
                if (ss[0] == '}')
                    break;

                if (!strncmp(ss, "index.html", 10))
                    c.index_html = 'y';
                else if (!strncmp(ss, "index.php", 9))
                    c.index_php = 'y';
                else if (!strncmp(ss, "index.pl", 8))
                    c.index_pl = 'y';
                else if (!strncmp(ss, "index.fcgi", 10))
                    c.index_fcgi = 'y';
                else
                    fprintf(stderr, "<%s:%d> Error read conf file(): \"index\" [%s]\n", __func__, __LINE__, ss), exit(1);
            }

            if (ss[0] != '}')
            {
                fprintf(stderr, "<%s:%d> Error read conf file(): \"index\" [%s]\n", __func__, __LINE__, ss);
                exit(1);
            }
        }
        else if (!strcmp(ss, "fastcgi"))
        {
            if ((n = find_str(f, "{")) < 0)
            {
                fprintf(stderr, "<%s:%d>   Error read config file, n=%d\n", __func__, __LINE__, n);
                exit(1);
            }

            while ((n = getLine(f, ss, sizeof(ss))) >= 0)
            {
                if (ss[0] == '}')
                    break;

                char s1[64], s2[256];
                if (sscanf(ss, "%63s %255s", s1, s2) != 2)
                {
                    fprintf(stderr, "<%s:%d> Error read conf file(): \"fastcgi\" [%s]\n", __func__, __LINE__, ss);
                    exit(1);
                }

                if (!prev)
                {
                    prev = c.fcgi_list = create_fcgi_list(s1, s2);
                }
                else
                {
                    fcgi_list_addr *tmp;
                    tmp = create_fcgi_list(s1, s2);
                    prev->next = tmp;
                    prev = tmp;
                }
            }

            if (ss[0] != '}')
            {
                fprintf(stderr, "<%s:%d> Error read_conf_file(): \"fastcgi\" [%s]\n", __func__, __LINE__, ss);
                free_fcgi_list();
                exit(1);
            }
        }
        else
        {
            fprintf(stderr, "<%s:%d> Error read conf file(): [%s]\n", __func__, __LINE__, ss);
            exit(1);
        }
    }

    fclose(f);

    fcgi_list_addr *i = c.fcgi_list;
    for (; i; i = i->next)
    {
        fprintf(stderr, "   [%s] : [%s]\n", i->scrpt_name, i->addr);
    }
    //------------------------------------------------------------------
    if (check_path(c.logDir, sizeof(c.logDir)) == -1)
    {
        fprintf(stderr, "!!! Error logDir [%s]\n", c.logDir);
        exit(1);
    }

    create_logfiles(c.logDir, c.ServerSoftware);
    //------------------------------------------------------------------
    if ((c.NumProc < 1) || (c.NumProc > 8))
    {
        fprintf(stderr, "<%s:%d> Error NumProc = %d; [1 < NumProc <= 6]\n", __func__, __LINE__, c.NumProc);
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
        long max_fd = (c.MAX_REQUESTS * 2) + 8;
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
        if (prev)
        {
            free(prev->scrpt_name);
            free(prev->addr);
            free(prev);
        }
    }
}
//======================================================================
void set_sndbuf(int n)
{
    c.SNDBUF_SIZE = n;
}
