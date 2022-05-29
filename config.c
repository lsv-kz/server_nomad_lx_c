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
void create_conf_file()
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
}
//======================================================================
int getLine(FILE *f, char *s, int size)
{
    char *p = s;
    int ch, len = 0, numWords = 0, wr = 1, wrSpace = 0;

    while (((ch = getc(f)) != EOF) && (len < size))
    {
        if ((char)ch == '\n')
        {
            if (len)
            {
                *p = 0;
                return ++numWords;
            }
            else
            {
                wr = 1;
                p = s;
                wrSpace = 0;
                continue;
            }
        }

        if (wr == 0)
            continue;

        switch (ch)
        {
            case ' ':
            case '\t':
                if (len)
                    wrSpace = 1;
            case '\r':
                break;
            case '#':
                wr = 0;
                break;
            case '{':
            case '}':
                if (len)
                    fseek(f, -1, 1);
                else
                {
                    *(p++) = (char)ch;
                    ++len;
                }

                *p = 0;
                return ++numWords;
            default:
                if (wrSpace)
                {
                    *(p++) = ' ';
                    ++len;
                    ++numWords;
                    wrSpace = 0;
                }

                *(p++) = (char)ch;
                ++len;
        }
    }

    *p = 0;
    if (len && (len < size))
        return ++numWords;
    return -1;
}
//======================================================================
int isnumber(const char *s)
{
    if (!s)
        return 0;
    int n = isdigit((int)*(s++));
    while (*s && n)
        n = isdigit((int)*(s++));
    return n;
}
//======================================================================
int isbool(const char *s)
{
    if (!s)
        return 0;
    if (strlen(s) != 1)
        return 0;
    return (((char)tolower(s[0]) == 'y') || ((char)tolower(s[0]) == 'n'));
}
//======================================================================
void create_fcgi_list(fcgi_list_addr **l, const char *s1, const char *s2)
{
    if (l == NULL)
    {
        fprintf(stderr, "<%s:%d> Error pointer = NULL\n", __func__, __LINE__);
        exit(errno);
    }

    fcgi_list_addr *t = malloc(sizeof(fcgi_list_addr));
    if (!t)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    t->scrpt_name = strdup(s1);
    t->addr = strdup(s2);

    if (!t->scrpt_name || !t->addr)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    t->next = *l;
    *l = t;
}
//======================================================================
int find_bracket(FILE *f)
{
    int ch, grid = 0;

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '#')
            grid = 1;
        else if (ch == '\n')
            grid = 0;
        else if ((ch == '{') && (grid == 0))
            return 1;
        else if ((ch != ' ') && (ch != '\t') && (grid == 0))
            return 0;
    }

    return 0;
}
//======================================================================
int read_conf_file(const char *path_conf)
{
    FILE *f;
    char ss[512];

    f = fopen(path_conf, "r");
    if (!f)
    {
        create_conf_file();
        fprintf(stderr, " Correct config file\n");
        exit(1);
    }

    c.index_html = c.index_php = c.index_pl = c.index_fcgi = 'n';
    c.fcgi_list = NULL;

    int n;
    while ((n = getLine(f, ss, sizeof(ss))) > 0)
    {
        if (n == 2)
        {
            char s1[128], s2[128];
            if (sscanf(ss, "%127s %127s", s1, s2) != 2)
            {
                fprintf(stderr, "<%s:%d> Error read config file: \"%s\"\n", __func__, __LINE__, ss);
                exit(1);
            }

            if (!strcmp(s1, "ServerAddr"))
                snprintf(c.host, sizeof(c.host), "%s", s2);
            else if (!strcmp(s1, "Port"))
                snprintf(c.servPort, sizeof(c.servPort), "%s", s2);
            else if (!strcmp(s1, "ServerSoftware"))
                snprintf(c.ServerSoftware, sizeof(c.ServerSoftware), "%s", s2);
            else if (!strcmp(s1, "tcp_cork") && isbool(s2))
                c.tcp_cork = (char)tolower(s2[0]);
            else if (!strcmp(s1, "SndBufSize") && isnumber(s2))
                sscanf(s2, "%d", &c.SNDBUF_SIZE);
            else if (!strcmp(s1, "SendFile") && isbool(s2))
                c.SEND_FILE = (char)tolower(s2[0]);
            else if (!strcmp(s1, "TcpNoDelay") && isbool(s2))
                c.TcpNoDelay = (char)tolower(s2[0]);
            else if (!strcmp(s1, "DocumentRoot"))
                snprintf(c.rootDir, sizeof(c.rootDir), "%s", s2);
            else if (!strcmp(s1, "ScriptPath"))
                snprintf(c.cgiDir, sizeof(c.cgiDir), "%s", s2);
            else if (!strcmp(s1, "LogPath"))
                snprintf(c.logDir, sizeof(c.logDir), "%s", s2);
            else if (!strcmp(s1, "MaxRequestsPerThr") && isnumber(s2))
                sscanf(s2, "%d", &c.MaxRequestsPerThr);
            else if (!strcmp(s1, "ListenBacklog") && isnumber(s2))
                sscanf(s2, "%d", &c.ListenBacklog);
            else if (!strcmp(s1, "MaxSndFd") && isnumber(s2))
                sscanf(s2, "%d", &c.MAX_SND_FD);
            else if (!strcmp(s1, "TimeoutPoll") && isnumber(s2))
                sscanf(s2, "%d", &c.TIMEOUT_POLL);
            else if (!strcmp(s1, "MaxRequests") && isnumber(s2))
                sscanf(s2, "%d", &c.MAX_REQUESTS);
            else if (!strcmp(s1, "NumProc") && isnumber(s2))
                sscanf(s2, "%d", &c.NumProc);
            else if (!strcmp(s1, "MaxThreads") && isnumber(s2))
                sscanf(s2, "%d", &c.MaxThreads);
            else if (!strcmp(s1, "MinThreads") && isnumber(s2))
                sscanf(s2, "%d", &c.MinThreads);
            else if (!strcmp(s1, "MaxProcCgi") && isnumber(s2))
                sscanf(s2, "%d", &c.MaxProcCgi);
            else if (!strcmp(s1, "KeepAlive") && isbool(s2))
                c.KeepAlive = (char)tolower(s2[0]);
            else if (!strcmp(s1, "TimeoutKeepAlive") && isnumber(s2))
                sscanf(s2, "%d", &c.TimeoutKeepAlive);
            else if (!strcmp(s1, "TimeOut") && isnumber(s2))
                sscanf(s2, "%d", &c.TimeOut);
            else if (!strcmp(s1, "TimeoutCGI") && isnumber(s2))
                sscanf(s2, "%d", &c.TimeoutCGI);
            else if (!strcmp(s1, "MaxRanges") && isnumber(s2))
                sscanf(s2, "%d", &c.MaxRanges);
            else if (!strcmp(s1, "UsePHP"))
                snprintf(c.UsePHP, sizeof(c.UsePHP), "%s", s2);
            else if (!strcmp(s1, "PathPHP"))
                snprintf(c.PathPHP, sizeof(c.PathPHP), "%s", s2);
            else if (!strcmp(s1, "ShowMediaFiles") && isbool(s2))
                c.ShowMediaFiles = (char)tolower(s2[0]);
            else if (!strcmp(s1, "Chunked") && isbool(s2))
                sscanf(s2, "%c", &c.Chunked);
            else if (!strcmp(s1, "ClientMaxBodySize") && isnumber(s2))
                sscanf(s2, "%ld", &c.ClientMaxBodySize);
            else if (!strcmp(s1, "User"))
                snprintf(c.user, sizeof(c.user), "%s", s2);
            else if (!strcmp(s1, "Group"))
                snprintf(c.group, sizeof(c.group), "%s", s2);
            else
            {
                fprintf(stderr, "<%s:%d> Error read conf file(): [%s]\n", __func__, __LINE__, ss);
                exit(1);
            }
        }
        else if (n == 1)
        {
            if (!strcmp(ss, "index"))
            {
                if (find_bracket(f) == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\"\n", __func__, __LINE__);
                    exit(1);
                }

                while (getLine(f, ss, sizeof(ss)) == 1)
                {
                    if (ss[0] == '}')
                        break;

                    if (ss[0] == '{')
                    {
                        fprintf(stderr, "<%s:%d> Error not found \"}\"\n", __func__, __LINE__);
                        exit(1);
                    }

                    if (!strcmp(ss, "index.html"))
                        c.index_html = 'y';
                    else if (!strcmp(ss, "index.php"))
                        c.index_php = 'y';
                    else if (!strcmp(ss, "index.pl"))
                        c.index_pl = 'y';
                    else if (!strcmp(ss, "index.fcgi"))
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
                if (find_bracket(f) == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\"\n", __func__, __LINE__);
                    exit(1);
                }

                while (getLine(f, ss, sizeof(ss)) == 2)
                {
                    char s1[64], s2[256];
                    if (sscanf(ss, "%63s %255s", s1, s2) != 2)
                    {
                        fprintf(stderr, "<%s:%d> Error read conf file(): \"fastcgi\" [%s]\n", __func__, __LINE__, ss);
                        exit(1);
                    }

                    create_fcgi_list(&c.fcgi_list, s1, s2);
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
