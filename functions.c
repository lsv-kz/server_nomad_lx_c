#include "server.h"
#include <stdarg.h>

//======================================================================
int get_time(char *s, int size_buf)
{
    time_t now = 0;
    struct tm t;

    if (!s)
    {
        fprintf(stderr, "<%s:%d>  Error get_time(): s = NULL\n", __func__, __LINE__);
        return 1;
    }

    time(&now);
    if (!gmtime_r(&now, &t))
    {
        fprintf(stderr, "<%s:%d>  Error gmtime_r()\n", __func__, __LINE__);
        *s = 0;
        return 1;
    }

    strftime(s, size_buf, "%a, %d %b %Y %H:%M:%S GMT", &t);//%z  %Z
    return 0;
}
//======================================================================
int log_time(char *s, int size_buf)
{
    time_t now = 0;
    struct tm t;

    if (!s)
    {
        fprintf(stderr, "<%s:%d>  Error get_time(): s = NULL\n", __func__, __LINE__);
        return 1;
    }

    time(&now);
    if (!gmtime_r(&now, &t))
    {
        fprintf(stderr, "<%s:%d>  Error gmtime_r()\n", __func__, __LINE__);
        *s = 0;
        return 1;
    }

    strftime(s, size_buf, "%d/%b/%Y:%H:%M:%S GMT", &t);//%z  %Z
    return 0;
}
//======================================================================
int to_lower(const char c)
{
    return c + ((c >= 'A') && (c <= 'Z') ? ('a' - 'A') : 0);
}
//======================================================================
const char *strstr_case(const char *s1, const char *s2)
{
    const char *p1, *p2;
    char c1, c2;

    if (!s1 || !s2) return NULL;
    if (*s2 == 0) return s1;

    int diff = ('a' - 'A');

    for (; ; ++s1)
    {
        c1 = *s1;
        if (!c1) break;
        c2 = *s2;
        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
        if (c1 == c2)
        {
            p1 = s1;
            p2 = s2;
            ++s1;
            ++p2;

            for (; ; ++s1, ++p2)
            {
                c2 = *p2;
                if (!c2) return p1;

                c1 = *s1;
                if (!c1) return NULL;

                c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
                c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
                if (c1 != c2)
                    break;
            }
        }
    }

    return NULL;
}
//======================================================================
int strlcmp_case(const char *s1, const char *s2, int len)
{
    char c1, c2;

    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    int diff = ('a' - 'A');

    for (; len > 0; --len, ++s1, ++s2)
    {
        c1 = *s1;
        c2 = *s2;
        if (!c1 && !c2) return 0;

        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;

        if (c1 != c2) return (c1 - c2);
    }

    return 0;
}
//======================================================================
int get_int_method(char *s)
{
    if (!memcmp(s, "GET", 3))
        return M_GET;
    else if (!memcmp(s, "POST", 4))
        return M_POST;
    else if (!memcmp(s, "HEAD", 4))
        return M_HEAD;
    else if (!memcmp(s, "OPTIONS", 7))
        return M_OPTIONS;
    else if (!memcmp(s, "CONNECT", 7))
        return M_CONNECT;
    else
        return 0;
}
//======================================================================
const char *get_str_method(int i)
{
    if (i == M_GET)
        return "GET";
    else if (i == M_POST)
        return "POST";
    else if (i == M_HEAD)
        return "HEAD";
    else if (i == M_OPTIONS)
        return "OPTIONS";
    else if (i == M_CONNECT)
        return "CONNECT";
    return "?";
}
//======================================================================
int get_int_http_prot(char *s)
{
    if (!memcmp(s, "HTTP/1.1", 8))
        return HTTP11;
    else if (!memcmp(s, "HTTP/1.0", 8))
        return HTTP10;
    else if (!memcmp(s, "HTTP/0.9", 8))
        return HTTP09;
    else if (!memcmp(s, "HTTP/2", 6))
        return HTTP2;
    else
        return 0;
}
//======================================================================
const char *get_str_http_prot(int i)
{
    if (i == HTTP11)
        return "HTTP/1.1";
    else if (i == HTTP10)
            return "HTTP/1.0";
    else if (i == HTTP09)
            return "HTTP/0.9";
    else if (i == HTTP2)
            return "HTTP/2";
    return "?";
}
//======================================================================
const char *get_str_operation(enum OPERATION_TYPE n)
{
    switch (n)
    {
        case READ_REQUEST:
            return "READ_REQUEST";
        case SEND_RESP_HEADERS:
            return "SEND_RESP_HEADERS";
        case SEND_ENTITY:
            return "SEND_ENTITY";
        case DYN_PAGE:
            return "DYN_PAGE";
    }

    return "?";
}
//======================================================================
const char *get_cgi_operation(enum CGI_OPERATION n)
{
    switch (n)
    {
        case CGI_CREATE_PROC:
            return "CGI_CREATE_PROC";
        case CGI_STDIN:
            return "CGI_STDIN";
        case CGI_READ_HTTP_HEADERS:
            return "CGI_READ_HTTP_HEADERS";
        case CGI_SEND_HTTP_HEADERS:
            return "CGI_SEND_HTTP_HEADERS";
        case CGI_SEND_ENTITY:
            return "CGI_SEND_ENTITY";
    }

    return "?";
}
//======================================================================
const char *get_fcgi_operation(enum FCGI_OPERATION n)
{
    switch (n)
    {
        case FASTCGI_CONNECT:
            return "FASTCGI_CONNECT";
        case FASTCGI_BEGIN:
            return "FASTCGI_BEGIN";
        case FASTCGI_PARAMS:
            return "FASTCGI_PARAMS";
        case FASTCGI_STDIN:
            return "FASTCGI_STDIN";
        case FASTCGI_READ_HTTP_HEADERS:
            return "FASTCGI_READ_HTTP_HEADERS";
        case FASTCGI_SEND_HTTP_HEADERS:
            return "FASTCGI_SEND_HTTP_HEADERS";
        case FASTCGI_SEND_ENTITY:
            return "FASTCGI_SEND_ENTITY";
        case FASTCGI_READ_ERROR:
            return "FASTCGI_READ_ERROR";
        case FASTCGI_CLOSE:
            return "FASTCGI_CLOSE";
    }

    return "?";
}
//======================================================================
const char *get_fcgi_status(enum FCGI_STATUS n)
{
    switch (n)
    {
        case FCGI_READ_DATA:
            return "FCGI_READ_DATA";
        case FCGI_READ_HEADER:
            return "FCGI_READ_HEADER";
        case FCGI_READ_PADDING:
            return "FCGI_READ_PADDING";
    }

    return "?";
}
//======================================================================
const char *get_scgi_operation(enum SCGI_OPERATION n)
{
    switch (n)
    {
        case SCGI_CONNECT:
            return "SCGI_CONNECT";
        case SCGI_PARAMS:
            return "SCGI_PARAMS";
        case SCGI_STDIN:
            return "SCGI_STDIN";
        case SCGI_READ_HTTP_HEADERS:
            return "SCGI_READ_HTTP_HEADERS";
        case SCGI_SEND_HTTP_HEADERS:
            return "SCGI_SEND_HTTP_HEADERS";
        case SCGI_SEND_ENTITY:
            return "SCGI_SEND_ENTITY";
    }

    return "?";
}
//======================================================================
const char *get_cgi_type(enum CGI_TYPE n)
{
    switch (n)
    {
        case NO_CGI:
            return "NO_CGI";
        case CGI:
            return "CGI";
        case PHPCGI:
            return "PHPCGI";
        case PHPFPM:
            return "PHPFPM";
        case FASTCGI:
            return "FASTCGI";
        case SCGI:
            return "SCGI";
    }

    return "?";
}
//======================================================================
const char *get_cgi_dir(enum DIRECT n)
{
    switch (n)
    {
        case FROM_CGI:
            return "FROM_CGI";
        case TO_CGI:
            return "TO_CGI";
        case FROM_CLIENT:
            return "FROM_CLIENT";
        case TO_CLIENT:
            return "TO_CLIENT";
    }

    return "?";
}
//======================================================================
char *strstr_lowercase(char * s1, char *s2)
{
    int i, len = strlen(s2);
    char *p = s1;
    for ( i = 0; *p; ++p)
    {
        if (tolower(*p) == tolower(s2[0]))
        {
            for ( i = 1; ; i++)
            {
                if (i == len)
                    return p;
                if (tolower(p[i]) != tolower(s2[i]))
                    break;
            }
        }
    }
    return NULL;
}
//======================================================================
int str_num(const char *s)
{
    unsigned int len = strlen(s), i = 0;

    while (i < len)
    {
        switch (*(s + i))
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                break;
            default:
                return 0;
        }
        i++;
    }
    sscanf(s, "%u", &i);
    return i;
}
//======================================================================
char *istextfile(const char *path)
{
    FILE *f;
    int cnt, i;
    int c;
    char s[128];
    char chr_txt[] = "`~!@#$%^&*()-_=+\\|[]{};:'\",<.>/?"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz"
                    "0123456789"
                    "\x09\x20\x0a\x0d";

    f = fopen(path, "r");
    if (f == NULL)
    {
        printf("error fopen\n");
        return "";
    }

    for (cnt = 0; ((c = fgetc(f)) >= 0) && (cnt < 128); cnt++)
    {
        if ((c < ' ') && (c != '\t') && (c != '\r') && (c != '\n'))
        {
            fclose(f);
            return "";
        }
    }

    fseek(f, 0, SEEK_SET);
    fgets(s, sizeof(s), f);
    if (strstr(s, "html>") || strstr(s, "HTML>") || strstr(s, "<html") || strstr(s, "<HTML"))
    {
        fclose(f);
        return "text/html";
    }

    fseek(f, 0, SEEK_SET);
    for (cnt = 0; ((c = fgetc(f)) >= 0) && (cnt < 32); cnt++)
    {
        if ((c < ' ') && (c != '\t') && (c != '\r') && (c != '\n'))
        {
            fclose(f);
            return "";
        }

        if (c < 0x7f)
        {
            if (!strchr(chr_txt, c))
            {
                fclose(f);
                return "";
            }
            continue;
        }
        else if ((c >= 0xc0) && (c <= 0xdf))
        {
            for (i = 1; i < 2; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                {
                    fclose(f);
                    goto end;
                    return "";
                }
            }
            continue;
        }
        else if ((c >= 0xe0) && (c <= 0xef))
        {
            for (i = 1; i < 3; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                {
                    fclose(f);
                    goto end;
                    return "";
                }
            }
            continue;
        }
        else if ((c >= 0xf0) && (c <= 0xf7))
        {
            for (i = 1; i < 4; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                {
                    fclose(f);
                    goto end;
                    return "";
                }
            }
            continue;
        }
        else if ((c >= 0xf8) && (c <= 0xfb))
        {
            for (i = 1; i < 5; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                {
                    fclose(f);
                    goto end;
                    return "";
                }
            }
            continue;
        }
        else if ((c >= 0xfc) && (c <= 0xfd))
        {
            for (i = 1; i < 6; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                {
                    fclose(f);
                    goto end;
                    return "";
                }
            }
            continue;
        }
        else
        {
            fclose(f);
            goto end;
            return "";
        }
        fclose(f);
        goto end;
        return "";
    }
    fclose(f);
    return "text/plain; charset=UTF-8";
end:
    return "";
}
//======================================================================
char *ismediafile(const char *path)
{
    FILE *f;
    int size = 0;
    char s[64];

    f = fopen(path, "r");
    if (f == NULL)
    {
        printf("error fopen\n");
        return "";
    }
    size = fread(s, 1, 63, f);
    fclose(f);
    if (size <= 0)
        return "";

    if (!memcmp(s, "\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C", 16))
    {
        return "video/x-ms-wmv";
    }

    if (s[0] == 'C' || s[0] == 'F')
    {
        if (!memcmp(s + 1, "WS", 2))
        {
            if (s[3] >= 0x02 && s[3] <= 0x15)
                return "application/x-shockwave-flash";
        }
    }

    if (!memcmp(s, "RIFF", 4))                              // avi, wav
    {
        if (!memcmp(s + 8, "AVI LIST", 8)) return "video/x-msvideo";
        else if (!memcmp(s + 8, "WAVE", 4)) return "audio/x-wav";
        else return "";
    }

    if ((!memcmp(s, "\xff\xf1", 2)) || (!memcmp(s, "\xff\xf9", 2))) return "audio/aac";
    if (!memcmp(s + 8, "AIFF", 4)) return "audio/aiff";
    if (!memcmp(s, "fLaC", 4)) return "audio/flac";
    if (!memcmp(s, "#!AMR", 4)) return "audio/amr";
    if (!memcmp(s, "ID3", 3)) return "audio/mpeg";          // mp3
    if (!memcmp(s, "MThd", 4)) return "audio/midi";
    if (!memcmp(s, "OggS", 4)) //return "audio/ogg";
    {
        if (!memcmp(s + 28, "\x01""vorbis", 7) || !memcmp(s + 28, "\x7f""FLAC", 5))
            return "audio/ogg";
    }

    if (*s == '\xff')
    {
        if (memchr("\xE2\xE3\xF2\xF3\xFA\xFB", s[1], 6))
        {
            if (((s[2] & 0xF0) != 0xF0) && ((s[2] & 0xF0) != 0x00) && ((s[2] & 0x0F) < 0x0C))
                return "audio/mpeg";
        }
    }

    if (!memcmp(s, "FLV", 3)) return "video/x-flv";            // flv
    if (!memcmp(s + 4, "ftyp3gp", 6)) return "video/3gpp"; // 3gp
    if (!memcmp(s + 4, "ftypqt", 6)) return "video/quicktime"; // mov
    if (!memcmp(s + 4, "ftyp", 4)) return "video/mp4";         // mp4
    if (!memcmp(s, "\x1A\x45\xDF\xA3", 4))    // \x93\x42\x82\x88
        return "video/x-matroska";                            // mkv
    if (!memcmp(s, "OggS", 4)) return "video/ogg";
    if (!memcmp(s + 4, "moov", 4)) return "video/quicktime";
    if (!memcmp(s, "\x00\x00\x01\xBA", 4)) return "video/mpeg";
    return "";
}
//======================================================================
char *content_type(const char *s)
{
    char *p;

    p = strrchr(s, '.');
    if (!p)
        goto end;

    //       video
    if (!strlcmp_case(p, ".ogv", 4)) return "video/ogg";
    else if (!strlcmp_case(p, ".mp4", 4)) return "video/mp4";
    else if (!strlcmp_case(p, ".avi", 4)) return "video/x-msvideo";
    else if (!strlcmp_case(p, ".mov", 4)) return "video/quicktime";
    else if (!strlcmp_case(p, ".mkv", 4)) return "video/x-matroska";
    else if (!strlcmp_case(p, ".flv", 4)) return "video/x-flv";
    else if (!strlcmp_case(p, ".mpeg", 5) || !strlcmp_case(p, ".mpg", 4)) return "video/mpeg";
    else if (!strlcmp_case(p, ".asf", 4)) return "video/x-ms-asf";
    else if (!strlcmp_case(p, ".wmv", 4)) return "video/x-ms-wmv";
    else if (!strlcmp_case(p, ".swf", 4)) return "application/x-shockwave-flash";
    else if (!strlcmp_case(p, ".3gp", 4)) return "video/video/3gpp";

    //       sound
    else if (!strlcmp_case(p, ".mp3", 4)) return "audio/mpeg";
    else if (!strlcmp_case(p, ".wav", 4)) return "audio/x-wav";
    else if (!strlcmp_case(p, ".ogg", 4)) return "audio/ogg";
    else if (!strlcmp_case(p, ".pls", 4)) return "audio/x-scpls";
    else if (!strlcmp_case(p, ".aac", 4)) return "audio/aac";
    else if (!strlcmp_case(p, ".aif", 4)) return "audio/x-aiff";
    else if (!strlcmp_case(p, ".ac3", 4)) return "audio/ac3";
    else if (!strlcmp_case(p, ".voc", 4)) return "audio/x-voc";
    else if (!strlcmp_case(p, ".flac", 5)) return "audio/flac";
    else if (!strlcmp_case(p, ".amr", 4)) return "audio/amr";
    else if (!strlcmp_case(p, ".au", 3)) return "audio/basic";

    //       image
    else if (!strlcmp_case(p, ".gif", 4)) return "image/gif";
    else if (!strlcmp_case(p, ".svg", 4) || !strlcmp_case(p, ".svgz", 5)) return "image/svg+xml";
    else if (!strlcmp_case(p, ".png", 4)) return "image/png";
    else if (!strlcmp_case(p, ".ico", 4)) return "image/vnd.microsoft.icon";
    else if (!strlcmp_case(p, ".jpeg", 5) || !strlcmp_case(p, ".jpg", 4)) return "image/jpeg";
    else if (!strlcmp_case(p, ".djvu", 5) || !strlcmp_case(p, ".djv", 4)) return "image/vnd.djvu";
    else if (!strlcmp_case(p, ".tiff", 5)) return "image/tiff";
    //       text
    else if (!strlcmp_case(p, ".txt", 4)) return istextfile(s);
    else if (!strlcmp_case(p, ".html", 5) || !strlcmp_case(p, ".htm", 4) || !strlcmp_case(p, ".shtml", 6)) return "text/html";
    else if (!strlcmp_case(p, ".css", 4)) return "text/css";

    //       application
    else if (!strlcmp_case(p, ".pdf", 4)) return "application/pdf";
    else if (!strlcmp_case(p, ".gz", 3)) return "application/gzip";
end:
    p = ismediafile(s);
    if (p)
        if (strlen(p)) return p;

    p = istextfile(s);
    if (p)
        if (strlen(p)) return p;

    return "";
}
//======================================================================
int clean_path(char *path)
{
    unsigned int num_subfolder = 0;
    const unsigned int max_subfolder = 20;
    int arr[max_subfolder];
    int i = 0, j = 0;
    char ch;

    while ((ch = *(path + j)))
    {
        if (!memcmp(path + j, "/../", 4))
        {
            if (num_subfolder)
                i = arr[--num_subfolder];
            else
                return -1;
            j += 3;
        }
        else if (!memcmp(path + j, "//", 2))
            j += 1;
        else if (!memcmp(path + j, "/./", 3))
            j += 2;
        else if (!memcmp(path + j, ".\0", 2))
            break;
        else if (!memcmp(path + j, "/..\0", 4))
        {
            if (num_subfolder)
            {
                i = arr[--num_subfolder];
                i++;
                break;
            }
            else
                return -1;
        }
        else
        {
            if (ch == '/')
            {
                if (num_subfolder < max_subfolder)
                    arr[num_subfolder++] = i;
                else
                    return -1;
            }
            
            *(path + i) = ch;
            ++i;
            ++j;
        }
    }
    
    *(path + i) = 0;

    return i;
}
//======================================================================
const char *base_name(const char *path)
{
    char *p;

    if (!path)
        return NULL;

    p = strrchr(path, '/');
    if (p)
        return p + 1;

    return path;
}
//======================================================================
int parse_startline_request(Connect *req, char *s)
{
    if (s == NULL)
    {
        print__err(req, "<%s:%d> Error: start line is empty\n",  __func__, __LINE__);
        return -1;
    }
    char *p = s, *p_val;
    //------------------------------ method ----------------------------
    if (*p == ' ')
        return -RS400;
    p_val = p;
    while (*p)
    {
        if (*p == ' ')
        {
            *(p++) = 0;
            break;
        }
        p++;
    }

    req->reqMethod = get_int_method(p_val);
    if (!req->reqMethod)
        return -RS400;
    //------------------------------- uri ------------------------------
    while (*p == ' ') p++;
    //if (*p == ' ') return -RS400;
    p_val = p;
    while (*p)
    {
        if (*p == ' ')
        {
            *(p++) = 0;
            break;
        }
        p++;
    }

    req->uri = p_val;
    //------------------------------ version ---------------------------
    while (*p == ' ') p++;
    //if (*p == ' ') return -RS400;
    p_val = p;
    while (*p)
    {
        if (*p == ' ')
        {
            *(p++) = 0;
            break;
        }
        p++;
    }

    if (!(req->httpProt = get_int_http_prot(p_val)))
    {
        print__err(req, "<%s:%d> Error version protocol\n", __func__, __LINE__);
        req->httpProt = HTTP11;
        return -RS400;
    }
    return 0;
}
//======================================================================
int parse_headers(Connect *req, char *pName, int i)
{
    if (pName == NULL)
    {
        print__err(req, "<%s:%d> Error: header is empty\n",  __func__, __LINE__);
        return -1;
    }

    if (req->httpProt == HTTP09)
    {
        print__err(req, "<%s:%d> Error version protocol\n", __func__, __LINE__);
        return -1;
    }

    char *pVal = pName, ch;
    int colon = 0;
    while ((ch = *pVal))
    {
        if (ch == ':')
            colon = 1;
        else if ((ch == ' ') || (ch == '\t') || (ch == '\n') || (ch == '\r'))
        {
            if (colon == 0)
                return -RS400;
            *(pVal++) = 0;
            break;
        }
        else
            *pVal = to_lower(ch);
        pVal++;
    }

    while (*pVal == ' ')
        pVal++;
    //------------------------------------------------------------------
    if (!strcmp(pName, "accept-encoding:"))
    {
        req->req_hd.iAcceptEncoding = i;
    }
    else if (!strcmp(pName, "connection:"))
    {
        req->req_hd.iConnection = i;
        if (strstr_case(pVal, "keep-alive"))
            req->connKeepAlive = 1;
        else
            req->connKeepAlive = 0;
    }
    else if (!strcmp(pName, "content-length:"))
    {
        req->req_hd.reqContentLength = atoll(pVal);
        req->req_hd.iContentLength = i;
    }
    else if (!strcmp(pName, "content-type:"))
    {
        req->req_hd.iReqContentType = i;
    }
    else if (!strcmp(pName, "host:"))
    {
        req->req_hd.iHost = i;
    }
    else if (!strcmp(pName, "if-range:"))
    {
        req->req_hd.iIf_Range = i;
    }
    else if (!strcmp(pName, "range:"))
    {
        char *p = strchr(pVal, '=');
        if (p)
            req->sRange = p + 1;
        else
            req->sRange = NULL;
        req->req_hd.iRange = i;
    }
    else if (!strcmp(pName, "referer:"))
    {
        req->req_hd.iReferer = i;
    }
    else if (!strcmp(pName, "upgrade:"))
    {
        req->req_hd.iUpgrade = i;
    }
    else if (!strcmp(pName, "user-agent:"))
    {
        req->req_hd.iUserAgent = i;
    }

    req->reqHeadersValue[i] = pVal;

    return 0;
}
//======================================================================
int find_empty_line(Connect *req)
{
    req->timeout = conf->Timeout;
    char *pCR, *pLF, ch;
    while (req->lenTail > 0)
    {
        int i = 0, len_line = 0;
        pCR = pLF = NULL;
        while (i < req->lenTail)
        {
            ch = *(req->p_newline + i);
            if (ch == '\r')// found CR
            {
                if (i == (req->lenTail - 1))
                    return 0;
                if (pCR)
                    return -RS400;
                pCR = req->p_newline + i;
            }
            else if (ch == '\n')// found LF
            {
                pLF = req->p_newline + i;
                if ((pCR) && ((pLF - pCR) != 1))
                    return -RS400;
                i++;
                break;
            }
            else
                len_line++;
            i++;
        }

        if (pLF) // found end of line '\n'
        {
            if (pCR == NULL)
                *pLF = 0;
            else
                *pCR = 0;

            if (len_line == 0) // found empty line
            {
                if (req->countReqHeaders == 0) // empty lines before Starting Line
                {
                    if ((pLF - req->bufReq + 1) > 4) // more than two empty lines
                        return -RS400;
                    req->lenTail -= i;
                    req->p_newline = pLF + 1;
                    continue;
                }

                if (req->lenTail > 0) // tail after empty line (Message Body for POST method)
                {
                    req->tail = pLF + 1;
                    req->lenTail -= i;
                }
                else
                    req->tail = NULL;
                return 1;
            }

            if (req->countReqHeaders < MAX_HEADERS)
            {
                req->reqHeadersName[req->countReqHeaders] = req->p_newline;
                if (req->countReqHeaders == 0)
                {
                    int ret = parse_startline_request(req, req->reqHeadersName[0]);
                    if (ret < 0)
                        return ret;
                }
                req->countReqHeaders++;
            }
            else
                return -RS500;

            req->lenTail -= i;
            req->p_newline = pLF + 1;
        }
        else if (pCR && (!pLF))
            return -RS400;
        else
            break;
    }

    return 0;
}
//======================================================================
void init_struct_request(Connect *req)
{
    req->bufReq[0] = '\0';
    req->decodeUri[0] = '\0';
    req->sLogTime[0] = '\0';
    req->uri = NULL;
    req->p_newline = req->bufReq;
    req->sReqParam = NULL;
    req->reqHeadersName[0] = NULL;
    req->reqHeadersValue[0] = NULL;
    req->lenBufReq = 0;
    req->lenTail = 0;
    req->reqMethod = 0;
    req->uriLen = 0;
    req->lenDecodeUri = 0;
    req->httpProt = 0;
    req->connKeepAlive = 0;
    req->err = 0;
    req->req_hd = (ReqHd){-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1LL};
    req->countReqHeaders = 0;
    req->cgi_type = NO_CGI;
    req->fileSize = -1LL;
    req->respStatus = 0;
    req->respContentLength = -1LL;
    req->respContentType = NULL;
    req->countRespHeaders = 0;
    req->send_bytes = 0LL;
    req->numPart = 0;
    req->sRange = NULL;
    req->rangeBytes = NULL;
    req->fd = -1;
    req->offset = 0;
    
    req->mode_send = NO_CHUNK;
    req->fcgi.size_par = req->fcgi.i_param = 0;
}
//======================================================================
void init_strings_request(Connect *r)
{
    StrInit(&r->resp_headers);
    StrInit(&r->hdrs);
    StrInit(&r->msg);
    StrInit(&r->html);
    StrInit(&r->scriptName);
    StrInit(&r->path);
}
//======================================================================
void free_strings_request(Connect *r)
{
    StrFree(&r->resp_headers);
    StrFree(&r->hdrs);
    StrFree(&r->msg);
    StrFree(&r->html);
    StrFree(&r->scriptName);
    StrFree(&r->path);
}
