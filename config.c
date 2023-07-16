#include "server.h"

static Config c;
const Config* const conf = &c;
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

    char path_new[PATH_MAX] = "";
    if (!realpath(path, path_new))
    {
        fprintf(stderr, "<%s:%d> Error realpath(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    snprintf(path, size, "%s", path_new);

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

    fprintf(f, "ServerSoftware  ? \n");
    fprintf(f, "ServerAddr  0.0.0.0\n");
    fprintf(f, "ServerPort  8080\n\n");

    fprintf(f, "DocumentRoot  ?\n");
    fprintf(f, "ScriptPath  ?\n");
    fprintf(f, "LogPath  ?\n");
    fprintf(f, "PidFilePath  ?\n\n");

    fprintf(f, "ListenBacklog  128\n");
    fprintf(f, "TcpCork     n  # n/y\n");
    fprintf(f, "TcpNoDelay  y  # n/y\n\n");

    fprintf(f, "SendFile  y  # n/y\n");
    fprintf(f, "SndBufSize  32768\n\n");

    fprintf(f, "MaxWorkConnections  768\n\n");

    fprintf(f, "BalancedLoad  4  # n/y\n\n");
    
    fprintf(f, "NumProc     4\n");
    fprintf(f, "NumThreads  2\n");
    fprintf(f, "MaxCgiProc  30\n\n");

    fprintf(f, "MaxRequestsPerClient  100\n");
    fprintf(f, "TimeoutKeepAlive  30\n");
    fprintf(f, "Timeout  60\n");
    fprintf(f, "TimeoutCGI  10\n");
    fprintf(f, "TimeoutPoll  100  # [ms]\n\n");

    fprintf(f, "MaxRanges  5\n\n");

    fprintf(f, "ClientMaxBodySize  5000000\n\n");

    fprintf(f, "UsePHP  n  # [n, php-fpm, php-cgi] \n");
    fprintf(f, "# PathPHP  127.0.0.1:9000  #  [php-fpm: 127.0.0.1:9000 | /var/run/php-fpm.sock; php-cgi: /usr/bin/php-cgi]\n\n");

    fprintf(f, "index {\n"
                "  #index.html\n"
                "}\n\n");

    fprintf(f, "fastcgi {\n"
                "  ~/env  127.0.0.1:9002\n"
                "}\n\n");

    fprintf(f, "scgi {\n"
                "\t#/scgi_test  127.0.0.1:9009\n"
                "}\n\n");

    fprintf(f, "ShowMediaFiles  N\n\n");

    fprintf(f, "User  root\n");
    fprintf(f, "Group  www-data\n");

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
static char bracket = 0;
//----------------------------------------------------------------------
int getLine(FILE *f, Line *ln)
{
    init_line(ln);
    char *p = ln->s;
    int ch, wr = 1, wr_space = 0, size = sizeof(ln->s);

    if (bracket)
    {
        *(p++) = bracket;
        *p = 0;
        bracket = 0;
        return 1;
    }

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
                bracket = (char)ch;
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
int find_bracket(FILE *f, char c)
{
    int ch, grid = 0;

    if (bracket)
    {
        if (c != bracket)
        {
            bracket = 0;
            return 0;
        }

        bracket = 0;
        return 1;
    }

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
int create_fcgi_list(fcgi_list_addr **l, const char *s1, const char *s2, enum CGI_TYPE type)
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

    t->script_name = strdup(s1);
    t->addr = strdup(s2);

    if (!t->script_name || !t->addr)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    t->type = type;
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
            err = get_word(&ln, c.ServerAddr, sizeof(c.ServerAddr));
        else if (!strcmp(s1, "ServerPort") && (n == 2))
            err = get_word(&ln, c.ServerPort, sizeof(c.ServerPort));
        else if (!strcmp(s1, "ServerSoftware") && (n == 2))
            err = get_word(&ln, c.ServerSoftware, sizeof(c.ServerSoftware));
        else if (!strcmp(s1, "DocumentRoot") && (n == 2))
            err = get_word(&ln, c.DocumentRoot, sizeof(c.DocumentRoot));
        else if (!strcmp(s1, "ScriptPath") && (n == 2))
            err = get_word(&ln, c.ScriptPath, sizeof(c.ScriptPath));
        else if (!strcmp(s1, "LogPath") && (n == 2))
            err = get_word(&ln, c.LogPath, sizeof(c.LogPath));
        else if (!strcmp(s1, "PidFilePath") && (n == 2))
            err = get_word(&ln, c.PidFilePath, sizeof(c.PidFilePath));
        else if (!strcmp(s1, "ListenBacklog") && (n == 2))
            err = get_int(&ln, &c.ListenBacklog);
        else if (!strcmp(s1, "TcpCork") && (n == 2))
            err = get_bool(&ln, &(c.TcpCork));
        else if (!strcmp(s1, "TcpNoDelay") && (n == 2))
            err = get_bool(&ln, &(c.TcpNoDelay));
        else if (!strcmp(s1, "SendFile") && (n == 2))
            err = get_bool(&ln, &(c.SendFile));
        else if (!strcmp(s1, "SndBufSize") && (n == 2))
            err = get_int(&ln, &c.SndBufSize);
        else if (!strcmp(s1, "MaxWorkConnections") && (n == 2))
            err = get_int(&ln, &c.MaxWorkConnections);
        else if (!strcmp(s1, "BalancedLoad") && (n == 2))
            err = get_bool(&ln, &(c.BalancedLoad));
        else if (!strcmp(s1, "NumProc") && (n == 2))
            err = get_int(&ln, (int*)&c.NumProc);
        else if (!strcmp(s1, "NumThreads") && (n == 2))
            err = get_int(&ln, (int*)&c.NumThreads);
        else if (!strcmp(s1, "MaxCgiProc") && (n == 2))
            err = get_int(&ln, (int*)&c.MaxCgiProc);
        else if (!strcmp(s1, "MaxRequestsPerClient") && (n == 2))
            err = get_int(&ln, &c.MaxRequestsPerClient);
        else if (!strcmp(s1, "TimeoutKeepAlive") && (n == 2))
            err = get_int(&ln, &c.TimeoutKeepAlive);
        else if (!strcmp(s1, "Timeout") && (n == 2))
            err = get_int(&ln, &c.Timeout);
        else if (!strcmp(s1, "TimeoutCGI") && (n == 2))
            err = get_int(&ln, &c.TimeoutCGI);
        else if (!strcmp(s1, "TimeoutPoll") && (n == 2))
            err = get_int(&ln, &c.TimeoutPoll);
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
            if (find_bracket(f, '{') == 0)
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
            if (find_bracket(f, '{') == 0)
            {
                fprintf(stderr, "<%s:%d> Error not found \"{\", line: %u\n", __func__, __LINE__, line_);
                return -1;
            }

            while (getLine(f, &ln) == 2)
            {
                char s2[256];
                if (get_word(&ln, s1, sizeof(s1)) <= 0)
                    return -1;

                if (get_word(&ln, s2, sizeof(s2)) <= 0)
                    return -1;

                if (create_fcgi_list(&c.fcgi_list, s1, s2, FASTCGI))
                    return -1;
            }

            if (ln.s[0] != '}')
            {
                fprintf(stderr, "<%s:%d> Error line: %u [%s]\n", __func__, __LINE__, line_, ln.s);
                return -1;
            }
        }
        else if (!strcmp(s1, "scgi") && (n == 1))
        {
            if (find_bracket(f, '{') == 0)
            {
                fprintf(stderr, "<%s:%d> Error not found \"{\", line: %u\n", __func__, __LINE__, line_);
                return -1;
            }

            while (getLine(f, &ln) == 2)
            {
                char s2[256];
                if (get_word(&ln, s1, sizeof(s1)) <= 0)
                    return -1;

                if (get_word(&ln, s2, sizeof(s2)) <= 0)
                    return -1;

                if (create_fcgi_list(&c.fcgi_list, s1, s2, SCGI))
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
    if (check_path(c.LogPath, sizeof(c.LogPath)) == -1)
    {
        fprintf(stderr, "!!! Error LogPath [%s]\n", c.LogPath);
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.DocumentRoot, sizeof(c.DocumentRoot)) == -1)
    {
        fprintf(stderr, "!!! Error DocumentRoot [%s]\n", c.DocumentRoot);
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.ScriptPath, sizeof(c.ScriptPath)) == -1)
    {
        c.ScriptPath[0] = '\0';
        fprintf(stderr, "!!! Error ScriptPath [%s]\n", c.ScriptPath);
    }
    //------------------------------------------------------------------
    if (check_path(c.PidFilePath, sizeof(c.PidFilePath)) == -1)
    {
        fprintf(stderr, "!!! Error PidFilePath [%s]\n", c.PidFilePath);
        return -1;
    }
    //------------------------------------------------------------------
    if (conf->SndBufSize <= 0)
    {
        fprintf(stderr, "<%s:%d> Error: SndBufSize=%d\n", __func__, __LINE__, conf->SndBufSize);
        exit(1);
    }
    //------------------------------------------------------------------
    if ((conf->NumProc < 1) || (conf->NumProc > PROC_LIMIT))
    {
        fprintf(stderr, "<%s:%d> Error: NumProc = %d; [1 <= NumProc <= 8]\n", __func__, __LINE__, conf->NumProc);
        return -1;
    }
    
    if (conf->NumProc == 1)
        c.BalancedLoad = 'n';
    //------------------------------------------------------------------
    if ((c.NumThreads > 8) || (c.NumThreads < 1))
    {
        fprintf(stderr, "<%s:%d> Error: NumThreads=%u\n", __func__, __LINE__, c.NumThreads);
        return -1;
    }
    //------------------------------------------------------------------
    if (c.MaxWorkConnections <= 0)
    {
        fprintf(stderr, "<%s:%d> Error: MaxWorkConnections=?\n", __func__, __LINE__);
        return -1;
    }

    const int fd_stdio = 3, fd_logs = 2, fd_serv_sock = 2, fd_pipe = 1; // 8
    long min_open_fd = fd_stdio + fd_logs + fd_serv_sock + fd_pipe;
    int max_fd = min_open_fd + c.MaxWorkConnections * 2;
    n = set_max_fd(max_fd);
    if (n < 0)
        return -1;
    else if (n < max_fd)
    {
        n = (n - min_open_fd)/2;
        c.MaxWorkConnections = n;
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
            free(prev->script_name);
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
        if (errno == ENOENT)
        {
            char s[8];
            printf("Create config file? [y/n]: ");
            fflush(stdout);
            fgets(s, sizeof(s), stdout);
            if (s[0] == 'y')
            {
                create_conf_file();
                fprintf(stderr, " Correct config file\n");
            }
        }
        else
            fprintf(stderr, "<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path_conf, strerror(errno));
        return -1;
    }

    int n = read_conf_file_(f);
    if (n)
        free_fcgi_list();
    fclose(f);
    return n;
}
//======================================================================
long get_lim_max_fd(long *max, long *cur)
{
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
        return -1;
    //printf(" .rlim_cur=%ld, .rlim_max=%ld\n", (long)lim.rlim_cur, (long)lim.rlim_max);
    *max = (long)lim.rlim_max;
    *cur = lim.rlim_cur;
    return 0;
}
//======================================================================
int set_max_fd(int max_open_fd)
{
    long max_fd, cur_fd;
    if (get_lim_max_fd(&max_fd, &cur_fd) == -1)
        return -1;

    if (max_open_fd > cur_fd)
    {
        struct rlimit lim;
        lim.rlim_max = max_fd;
        if (max_open_fd > max_fd)
            lim.rlim_cur = max_fd;
        else
            lim.rlim_cur = max_open_fd;

        if (setrlimit(RLIMIT_NOFILE, &lim) == -1)
            fprintf(stderr, "<%s:%d> Error setrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
        max_open_fd = sysconf(_SC_OPEN_MAX);
        if (max_open_fd < 0)
        {
            fprintf(stderr, "<%s:%d> Error sysconf(_SC_OPEN_MAX): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }

    //fprintf(stderr, "<%s:%d> max_open_fd=%d\n", __func__, __LINE__, max_open_fd);
    return max_open_fd;
}
