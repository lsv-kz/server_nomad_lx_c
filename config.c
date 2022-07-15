#include "server.h"

static Config c;
const Config* const conf = &c;
const int min_open_fd = 8;
//======================================================================
int check_path(char *path, int size)
{
    struct stat st;

    int ret = stat(path, &st);
    if (ret == -1)
    {
        fprintf(stderr, "<%s:%d> Error stat(%s): %s\n", __func__, __LINE__, path, strerror(errno));
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
    fprintf(f, "Port         ? \n");
    fprintf(f, "ServerSoftware   ? \n");

    fprintf(f, "tcp_cork   n \n");

    fprintf(f, "SndBufSize   32768\n");
    fprintf(f, "TcpNoDelay   y \n");

    fprintf(f, "SendFile   n \n");
    fprintf(f, "SendFileSizePart   1000000\n\n");

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

    fprintf(f, "User nobody     # www-data\n");
    fprintf(f, "Group nogroup   # www-data\n\n");

    fclose(f);
}
//======================================================================
typedef struct {
    char s[256], *p;
    int num_words;
    int len;
} Line;
//----------------------------------------------------------------------
void init_line(Line *l)
{
    l->s[0] = 0;
    l->p = l->s;
    l->num_words = 0;
    l->len = 0;
}
//----------------------------------------------------------------------
int get_word(Line *l, char *s, int size)
{
    if (!l || !s || (size <= 0))
        return -1;
    if (!l->num_words)
        return 0;
    int i = 0;
    char ch;
    while ((i < size) && *(l->p))
    {
        ch = *(l->p++);
        if (ch == '\r')
            continue;
        else if ((ch == ' ') || (ch == '\t') || (ch == '\n'))
        {
            if (i > 0)
                break;
        }
        else
            s[i++] = ch;
    }

    if (i < size)
    {
        l->num_words--;
        s[i] = 0;
        return i;
    }

    return -1;
}
//----------------------------------------------------------------------
int get_long(Line *l, long *li)
{
    if (!l || !li)
        return -1;
    char s[32], *p = s;
    int n;
    if ((n = get_word(l, s, sizeof(s))) <= 0)
        return -1;

    n = isdigit((int)*(p++));
    while (*p && n)
        n = isdigit((int)*(p++));
    if (!n)
        return -1;

    *li = atol(s);
    return 1;
}
//----------------------------------------------------------------------
int get_int(Line *l, int *li)
{
    if (!l || !li)
        return -1;
    char s[32], *p = s;
    int n;
    if ((n = get_word(l, s, sizeof(s))) <= 0)
        return -1;

    n = isdigit((int)*(p++));
    while (*p && n)
        n = isdigit((int)*(p++));
    if (!n)
        return -1;

    *li = atol(s);
    return 1;
}
//----------------------------------------------------------------------
int get_bool(Line *l, char *c)
{
    if (!l || !c)
        return -1;
    char s[2];
    int n;
    if ((n = get_word(l, s, sizeof(s))) != 1)
        return -1;

    if ((tolower(s[0]) == 'y') || (tolower(s[0]) == 'n'))
        *c = tolower(s[0]);
    else
        return -1;
    return 1;
}
//======================================================================
static int line_ = 1, line_inc = 0;
//----------------------------------------------------------------------
int getLine(FILE *f, Line *ln)
{
    init_line(ln);
    char *p = ln->s;
    int ch, wr = 1, wr_space = 0, size = sizeof(ln->s);
    
    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF) && (ln->len < size))
    {
        if (ch == '\n')
        {
            if (ln->len)
            {
                line_inc = 1;
                *p = 0;
                return ++ln->num_words;
            }
            else
            {
                ++line_;
                wr = 1;
                p = ln->s;
                wr_space = 0;
                continue;
            }
        }
        else if (wr == 0)
            continue;
        else if ((ch == ' ') || (ch == '\t'))
        {
            if (ln->len)
                wr_space = 1;
        }
        else if (ch == '#')
            wr = 0;
        else if ((ch == '{') || (ch == '}'))
        {
            if (ln->len)
                fseek(f, -1, 1);
            else
            {
                *(p++) = ch;
                ln->len++;
            }
            *p = 0;
            return ++ln->num_words;
        }
        else if (ch != '\r')
        {
            if (wr_space)
            {
                *(p++) = ' ';
                ln->len++;
                ln->num_words++;
                wr_space = 0;
            }

            *(p++) = (char)ch;
            ln->len++;
        }
    }

    if (ln->len == 0)
        return 0;
    else if (ln->len < size)
    {
        *p = 0;
        return ++ln->num_words;
    }
    else
        return -1;
}
//======================================================================
int find_bracket(FILE *f)
{
    int ch, grid = 0;
    
    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '#')
            grid = 1;
        else if (ch == '\n')
        {
            ++line_;
            grid = 0;
        }
        else if ((ch == '{') && (grid == 0))
            return 1;
        else if ((ch != ' ') && (ch != '\t') && (grid == 0))
            return 0;
    }

    return 0;
}
//======================================================================
int create_fcgi_list(fcgi_list_addr **l, const char *s1, const char *s2)
{
    if (l == NULL)
    {
        fprintf(stderr, "<%s:%d> Error pointer = NULL\n", __func__, __LINE__);
        return -1;
    }

    fcgi_list_addr *t = malloc(sizeof(fcgi_list_addr));
    if (!t)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    t->scrpt_name = strdup(s1);
    t->addr = strdup(s2);

    if (!t->scrpt_name || !t->addr)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    t->next = *l;
    *l = t;
    return 0;
}
//======================================================================
int read_conf_file_(FILE *f)
{
    char s1[512];
    Line ln;

    c.index_html = c.index_php = c.index_pl = c.index_fcgi = 'n';
    c.fcgi_list = NULL;

    int n, err;
    while ((n = getLine(f, &ln)) > 0)
    {
        if (get_word(&ln, s1, sizeof(s1)) <= 0)
            return -1;

        if (!strcmp(s1, "ServerAddr") && (n == 2))
            err = get_word(&ln, c.host, sizeof(c.host));
        else if (!strcmp(s1, "Port") && (n == 2))
            err = get_word(&ln, c.servPort, sizeof(c.servPort));
        else if (!strcmp(s1, "ServerSoftware") && (n == 2))
            err = get_word(&ln, c.ServerSoftware, sizeof(c.ServerSoftware));
        else if (!strcmp(s1, "tcp_cork") && (n == 2))
            err = get_bool(&ln, &(c.tcp_cork));
        else if (!strcmp(s1, "SndBufSize") && (n == 2))
            err = get_int(&ln, &c.SNDBUF_SIZE);
        else if (!strcmp(s1, "SendFile") && (n == 2))
            err = get_bool(&ln, &(c.SEND_FILE));
        else if (!strcmp(s1, "SendFileSizePart") && (n == 2))
            err = get_long(&ln, &(c.SEND_FILE_SIZE_PART));
        else if (!strcmp(s1, "TcpNoDelay") && (n == 2))
            err = get_bool(&ln, &(c.TcpNoDelay));
        else if (!strcmp(s1, "DocumentRoot") && (n == 2))
            err = get_word(&ln, c.rootDir, sizeof(c.rootDir));
        else if (!strcmp(s1, "ScriptPath") && (n == 2))
            err = get_word(&ln, c.cgiDir, sizeof(c.cgiDir));
        else if (!strcmp(s1, "LogPath") && (n == 2))
            err = get_word(&ln, c.logDir, sizeof(c.logDir));
        else if (!strcmp(s1, "MaxRequestsPerThr") && (n == 2))
            err = get_int(&ln, &c.MaxRequestsPerThr);
        else if (!strcmp(s1, "ListenBacklog") && (n == 2))
            err = get_int(&ln, &c.ListenBacklog);
        else if (!strcmp(s1, "MaxSndFd") && (n == 2))
            err = get_int(&ln, &c.MAX_SND_FD);
        else if (!strcmp(s1, "TimeoutPoll") && (n == 2))
            err = get_int(&ln, &c.TIMEOUT_POLL);
        else if (!strcmp(s1, "MaxRequests") && (n == 2))
            err = get_int(&ln, &c.MAX_REQUESTS);
        else if (!strcmp(s1, "NumProc") && (n == 2))
            err = get_int(&ln, &c.NumProc);
        else if (!strcmp(s1, "MaxThreads") && (n == 2))
            err = get_int(&ln, &c.MaxThreads);
        else if (!strcmp(s1, "MinThreads") && (n == 2))
            err = get_int(&ln, &c.MinThreads);
        else if (!strcmp(s1, "MaxProcCgi") && (n == 2))
            err = get_int(&ln, &c.MaxProcCgi);
        else if (!strcmp(s1, "KeepAlive") && (n == 2))
            err = get_bool(&ln, &c.KeepAlive);
        else if (!strcmp(s1, "TimeoutKeepAlive") && (n == 2))
            err = get_int(&ln, &c.TimeoutKeepAlive);
        else if (!strcmp(s1, "TimeOut") && (n == 2))
            err = get_int(&ln, &c.TimeOut);
        else if (!strcmp(s1, "TimeoutCGI") && (n == 2))
            err = get_int(&ln, &c.TimeoutCGI);
        else if (!strcmp(s1, "MaxRanges") && (n == 2))
            err = get_int(&ln, &c.MaxRanges);
        else if (!strcmp(s1, "UsePHP") && (n == 2))
            err = get_word(&ln, c.UsePHP, sizeof(c.UsePHP));
        else if (!strcmp(s1, "PathPHP") && (n == 2))
            err = get_word(&ln, c.PathPHP, sizeof(c.PathPHP));
        else if (!strcmp(s1, "ShowMediaFiles") && (n == 2))
            err = get_bool(&ln, &c.ShowMediaFiles);
        else if (!strcmp(s1, "ClientMaxBodySize") && (n == 2))
            err = get_long(&ln, &c.ClientMaxBodySize);
        else if (!strcmp(s1, "User") && (n == 2))
            err = get_word(&ln, c.user, sizeof(c.user));
        else if (!strcmp(s1, "Group") && (n == 2))
            err = get_word(&ln, c.group, sizeof(c.group));
        else if (!strcmp(s1, "index") && (n == 1))
        {
            if (find_bracket(f) == 0)
            {
                fprintf(stderr, "<%s:%d> Error not found \"{\"\n", __func__, __LINE__);
                return -1;
            }

            while (getLine(f, &ln) == 1)
            {
                if (ln.s[0] == '}')
                    break;

                if (!strcmp(ln.s, "index.html"))
                    c.index_html = 'y';
                else if (!strcmp(ln.s, "index.php"))
                    c.index_php = 'y';
                else if (!strcmp(ln.s, "index.pl"))
                    c.index_pl = 'y';
                else if (!strcmp(ln.s, "index.fcgi"))
                    c.index_fcgi = 'y';
                else
                {
                    fprintf(stderr, "<%s:%d> Error directive: [%s] line: %u\n", __func__, __LINE__, ln.s, line_);
                    return -1;
                }
            }

            if (ln.s[0] != '}')
            {
                fprintf(stderr, "<%s:%d> Error line: %u [%s]\n", __func__, __LINE__, line_, ln.s);
                return -1;
            }
        }
        else if (!strcmp(s1, "fastcgi") && (n == 1))
        {
            if (find_bracket(f) == 0)
            {
                fprintf(stderr, "<%s:%d> Error not found \"{\"\n", __func__, __LINE__);
                return -1;
            }

            while (getLine(f, &ln) == 2)
            {
                char s2[256];
                if (get_word(&ln, s1, sizeof(s1)) <= 0)
                    return -1;
                    
                if (get_word(&ln, s2, sizeof(s2)) <= 0)
                    return -1;

                if (create_fcgi_list(&c.fcgi_list, s1, s2))
                    return -1;
            }

            if (ln.s[0] != '}')
            {
                fprintf(stderr, "<%s:%d> Error line: %u [%s]\n", __func__, __LINE__, line_, ln.s);
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "<%s:%d> Error [%s] line: %u\n", __func__, __LINE__, ln.s, line_);
            return -1;
        }

        if (err <= 0)
        {
            fprintf(stderr, "<%s:%d> Error directive [%s] line: %u\n", __func__, __LINE__, ln.s, line_);
            return -1;
        }
    }

    if (n != 0)
    {
        fprintf(stderr, "<%s:%d> Error read config file: n=%d\n", __func__, __LINE__, n);
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.logDir, sizeof(c.logDir)) == -1)
    {
        fprintf(stderr, "!!! Error logDir [%s]\n", c.logDir);
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.rootDir, sizeof(c.rootDir)) == -1)
    {
        fprintf(stderr, "!!! Error rootDir [%s]\n", c.rootDir);
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.cgiDir, sizeof(c.cgiDir)) == -1)
    {
        c.cgiDir[0] = '\0';
        fprintf(stderr, "!!! Error ScriptPath [%s]\n", c.cgiDir);
    }
    //------------------------------------------------------------------
    if ((c.NumProc < 1) || (c.NumProc > 8))
    {
        fprintf(stderr, "<%s:%d> Error NumProc = %d; [1 < NumProc <= 6]\n", __func__, __LINE__, c.NumProc);
        return -1;
    }

    if (c.MinThreads > c.MaxThreads)
    {
        fprintf(stderr, "<%s:%d> Error: NumThreads > MaxThreads\n", __func__, __LINE__);
        return -1;
    }

    if (c.MinThreads < 1)
        c.MinThreads = 1;

    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
    {
        fprintf(stderr, "<%s:%d> Error getrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
    }
    else
    {
        long max_fd = (c.MAX_REQUESTS * 2) + min_open_fd;
        if (max_fd > (long)lim.rlim_cur)
        {
            if (max_fd > (long)lim.rlim_max)
                lim.rlim_cur = lim.rlim_max;
            else
                lim.rlim_cur = max_fd;

            if (setrlimit(RLIMIT_NOFILE, &lim) == -1)
                fprintf(stderr, "<%s:%d> Error setrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
            max_fd = sysconf(_SC_OPEN_MAX);
            if (max_fd > 1)
            {
                c.MAX_REQUESTS = (max_fd - min_open_fd)/2;
                fprintf(stderr, "<%s:%d> MAX_REQUESTS=%d, max_fd=%ld\n", __func__, __LINE__, c.MAX_REQUESTS, max_fd);
            }
            else
            {
                fprintf(stderr, "<%s:%d> Error sysconf(_SC_OPEN_MAX): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
        }
    }
    return 0;
}
//======================================================================
int set_uid()
{
    uid_t uid = getuid();
    if (uid == 0)
    {
        char *p;
        c.server_uid = strtol(conf->user, &p, 0);
        if (*p == '\0')
        {
            struct passwd *passwdbuf = getpwuid(conf->server_uid);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwuid(%u): %s\n", __func__, __LINE__, conf->server_uid, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct passwd *passwdbuf = getpwnam(conf->user);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwnam(%s): %s\n", __func__, __LINE__, conf->user, strerror(errno));
                return -1;
            }
            c.server_uid = passwdbuf->pw_uid;
        }

        c.server_gid = strtol(conf->group, &p, 0);
        if (*p == '\0')
        {
            struct group *groupbuf = getgrgid(conf->server_gid);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrgid(%u): %s\n", __func__, __LINE__, conf->server_gid, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct group *groupbuf = getgrnam(conf->group);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrnam(%s): %s\n", __func__, __LINE__, conf->group, strerror(errno));
                return -1;
            }
            c.server_gid = groupbuf->gr_gid;
        }
        //--------------------------------------------------------------
        if (conf->server_uid != uid)
        {
            if (setuid(conf->server_uid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setuid(%u): %s\n", __func__, __LINE__, conf->server_uid, strerror(errno));
                return -1;
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
int read_conf_file(const char *path_conf)
{
    FILE *f = fopen(path_conf, "r");
    if (!f)
    {
        int errno_ = errno;
        fprintf(stderr, "<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path_conf, strerror(errno_));
        if (errno == ENOENT)
        {
            create_conf_file();
            fprintf(stderr, " Correct config file\n");
        }
        exit(1);
    }

    int n;
    if ((n = read_conf_file_(f)))
        free_fcgi_list();
    fclose(f);
    return n;
}
