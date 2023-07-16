#include "server.h"

int response2(Connect *req);
//======================================================================
void response1(int num_proc)
{
    const char *p;
    Connect *req;

    while (1)
    {
        req = pop_resp_list();
        if (!req)
        {
            print_err("[%d]<%s:%d> Error pop_resp_list()=NULL\n", num_proc, __func__, __LINE__);
            return;
        }
        //--------------------------------------------------------------
        get_time(req->sLogTime, sizeof(req->sLogTime));
        for (int i = 1; i < req->countReqHeaders; ++i)
        {
            int ret = parse_headers(req, req->reqHeadersName[i], i);
            if (ret < 0)
            {
                print__err(req, "<%s:%d>  Error parse_headers(): %d\n", __func__, __LINE__, ret);
                goto end;
            }
        }
        //--------------------------------------------------------------
    #ifdef TCP_CORK_
        if (conf->TcpCork == 'y')
        {
        #if defined(LINUX_)
            int optval = 1;
            setsockopt(req->clientSocket, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
        #elif defined(FREEBSD_)
            int optval = 1;
            setsockopt(req->clientSocket, IPPROTO_TCP, TCP_NOPUSH, &optval, sizeof(optval));
        #endif
        }
    #endif
        //--------------------------------------------------------------
        if ((req->httpProt == HTTP09) || (req->httpProt == HTTP2))
        {
            req->connKeepAlive = 0;
            req->err = -RS505;
            goto end;
        }

        if (req->numReq >= conf->MaxRequestsPerClient || (req->httpProt == HTTP10))
            req->connKeepAlive = 0;
        else if (req->req_hd.iConnection == -1)
            req->connKeepAlive = 1;

        if ((p = strchr(req->uri, '?')))
        {
            req->uriLen = p - req->uri;
            req->sReqParam = req->uri + req->uriLen + 1;
        }
        else
        {
            if ((p = strstr_case(req->uri, "%3F")))
            {
                req->uriLen = p - req->uri;
                req->sReqParam = req->uri + req->uriLen + 3;
            }
            else
            {
                req->sReqParam = NULL;
                req->uriLen = strlen(req->uri);
            }
        }

        if (decode(req->uri, req->uriLen, req->decodeUri, sizeof(req->decodeUri)) <= 0)
        {
            print__err(req, "<%s:%d> Error: decode URI\n", __func__, __LINE__);
            req->err = -RS404;
            goto end;
        }
        
        if (clean_path(req->decodeUri) <= 0)
        {
            print__err(req, "<%s:%d> Error URI=%s\n", __func__, __LINE__, req->decodeUri);
            req->lenDecodeUri = strlen(req->decodeUri);
            req->err = -RS400;
            goto end;
        }
        req->lenDecodeUri = strlen(req->decodeUri);

        if (strstr(req->uri, ".php") && strcmp(conf->UsePHP, "php-cgi") && strcmp(conf->UsePHP, "php-fpm"))
        {
            print__err(req, "<%s:%d> 404\n", __func__, __LINE__);
            req->err = -RS404;
            goto end;
        }

        if (req->req_hd.iUpgrade >= 0)
        {
            print__err(req, "<%s:%d> req->upgrade: %s\n",  __func__, __LINE__, req->reqHeadersValue[req->req_hd.iUpgrade]);
            req->connKeepAlive = 0;
            req->err = -RS505;
            goto end;
        }
        //--------------------------------------------------------------
        if ((req->reqMethod == M_GET) || (req->reqMethod == M_HEAD) || (req->reqMethod == M_POST))
        {
            StrCpy(&req->path, conf->DocumentRoot);
            StrCat(&req->path, req->decodeUri);
            if (req->path.err == 0)
            {
                int ret = response2(req);
                if (ret == 1)
                {// "req" may be free
                    continue;
                }

                req->err = ret;
            }
            else
            {
                print_err("<%s:%d> Error: malloc()\n", __func__, __LINE__);
                req->err = -1;
            }
        }
        else if (req->reqMethod == M_OPTIONS)
            req->err = options(req);
        else
            req->err = -RS501;

    end:
        end_response(req);
    }
}
//======================================================================
void *thread_client(void *arg)
{
    int num_chld = *((int*)arg);
    response1(num_chld);
    return NULL;
}
//======================================================================
int read_dir(Connect *req);
int get_ranges(Connect *req);
static int send_multypart(Connect *req);
const char *boundary = "---------a9b5r7a4c0a2d5a1b8r3a";
//======================================================================
long long file_size(const char *s)
{
    struct stat st;

    if (!stat(s, &st))
        return st.st_size;
    else
        return -1;
}
//======================================================================
int fastcgi(Connect* req, const char* uri)
{
    const char* p = strrchr(uri, '/');
    if (!p)
        return -RS404;

    fcgi_list_addr* i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        if (i->script_name[0] == '~')
        {
            if (!strcmp(p, i->script_name + 1))
                break;
        }
        else
        {
            if (!strcmp(uri, i->script_name))
                break;
        }
    }

    if (!i)
        return -RS404;
    if (i->type == FASTCGI)
        req->cgi_type = FASTCGI;
    else if (i->type == SCGI)
        req->cgi_type = SCGI;
    else
        return -RS404;
    StrCpy(&req->scriptName, i->script_name);
    if (req->scriptName.err)
    {
        return -RS500;
    }
    push_cgi(req);
    return 1;
}
//======================================================================
int response2(Connect *req)
{
    struct stat st;
    char *p = strstr(req->decodeUri, ".php");
    if (p && (*(p + 4) == 0))
    {
        int ret;
        if (strcmp(conf->UsePHP, "php-cgi") && strcmp(conf->UsePHP, "php-fpm"))
        {
            print__err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        if (stat(req->decodeUri + 1, &st) == -1)
        {
            print__err(req, "<%s:%d> script (%s) not found\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        if (!strcmp(conf->UsePHP, "php-fpm"))
        {
            StrCpy(&req->scriptName, req->decodeUri);
            if (req->scriptName.err)
            {
                return -RS500;
            }
            req->cgi_type = PHPFPM;
            push_cgi(req);
            return 1;
        }
        else if (!strcmp(conf->UsePHP, "php-cgi"))
        {
            StrCpy(&req->scriptName, req->decodeUri);
            if (req->scriptName.err)
            {
                return -RS500;
            }
            req->cgi_type = PHPCGI;
            push_cgi(req);
            return 1;
        }
        else
        {
            print__err(req, "<%s:%d> PHP not implemented\n", __func__, __LINE__);
            ret = -RS500;
        }

        return ret;
    }

    if (!strncmp(req->decodeUri, "/cgi-bin/", 9)
        || !strncmp(req->decodeUri, "/cgi/", 5))
    {
        StrCpy(&req->scriptName, req->decodeUri);
        if (req->scriptName.err)
        {
            return -RS500;
        }
        req->cgi_type = CGI;
        push_cgi(req);
        return 1;
    }
    //------------------------------------------------------------------
    if (lstat(req->path.ptr, &st) == -1)
    {
        if (errno == EACCES)
            return -RS403;
        return fastcgi(req, req->decodeUri);
    }
    else
    {
        if ((!S_ISDIR(st.st_mode)) && (!S_ISREG(st.st_mode)))
        {
            print__err(req, "<%s:%d> Error: file (!S_ISDIR && !S_ISREG) \n", __func__, __LINE__);
            return -RS403;
        }
    }
    //------------------------------------------------------------------
    if (S_ISDIR(st.st_mode))
    {
        if (req->uri[req->uriLen - 1] != '/')
        {
            req->uri[req->uriLen++] = '/';
            req->uri[req->uriLen] = '\0';
            req->respStatus = RS301;

            StrReserve(&req->hdrs, 200);
            StrCpy(&req->hdrs, "Location: ");
            StrCatLN(&req->hdrs, req->uri);
            if (req->hdrs.err)
            {
                print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
                return -1;
            }

            StrReserve(&req->msg, 128);
            StrCpy(&req->msg, "The document has moved <a href=\"");
            StrCat(&req->msg, req->uri);
            StrCat(&req->msg, "\">here</a>.");
            if (req->msg.err)
            {
                print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
                return -1;
            }

            if (create_message(req, req->msg.ptr) == 1)
                return 1;
            else
                return -1;
        }
        //..............................................................
        int lenPath = StrLen(&req->path);
        StrCat(&req->path, "index.html");
        if (req->path.err)
        {
            print__err(req, "<%s:%d> ---\n", __func__, __LINE__);
            return -RS500;
        }

        if ((stat(req->path.ptr, &st) != 0) || (conf->index_html != 'y'))
        {
            errno = 0;

            if (strcmp(conf->UsePHP, "n") && (conf->index_php == 'y'))
            {
                StrResize(&req->path, lenPath);
                const char *NameScript = "index.php";
                StrCat(&req->path, "index.php");
                if (req->path.err)
                {
                    return -RS500;
                }
                if (!stat(req->path.ptr, &st))
                {
                    StrReserve(&req->scriptName, strlen(NameScript) + 1);
                    StrCpy(&req->scriptName, req->decodeUri);
                    StrCat(&req->scriptName, NameScript);
                    if (req->scriptName.err)
                    {
                        return -RS500;
                    }

                    if (!strcmp(conf->UsePHP, "php-fpm"))
                    {
                        req->cgi_type = PHPFPM;
                        push_cgi(req);
                        return 1;
                    }
                    else if (!strcmp(conf->UsePHP, "php-cgi"))
                    {
                        req->cgi_type = PHPCGI;
                        push_cgi(req);
                        return 1;
                    }
                    else
                    {
                        print__err(req, "<%s:%d> Error UsePHP: %s\n", __func__, __LINE__, conf->UsePHP);
                        return -1;
                    }
                }
            }

            StrResize(&req->path, lenPath);

            if (conf->index_pl == 'y')
            {
                req->cgi_type = CGI;
                StrCpy(&req->scriptName, "/cgi-bin/index.pl");
                if (req->scriptName.err)
                {
                    return -RS500;
                }

                push_cgi(req);
                return 1;
            }
            else if (conf->index_fcgi == 'y')
            {
                req->cgi_type = FASTCGI;
                StrCpy(&req->scriptName, "/index.fcgi");
                if (req->scriptName.err)
                {
                    return -RS500;
                }

                push_cgi(req);
                return 1;
            }

            return read_dir(req);
        }
    }

    if (req->reqMethod == M_POST)
        return -RS405;
    //----------------------- send file --------------------------------
    req->fileSize = file_size(req->path.ptr);
    req->numPart = 0;
    req->respContentType = content_type(req->path.ptr);

    if (req->req_hd.iRange >= 0)
    {
        int ret = get_ranges(req);
        if (ret < 0)
        {
            return ret;
        }

        req->respStatus = RS206;
        if (req->numPart == 1)
        {
            req->offset = req->rangeBytes[0].start;
            req->respContentLength = req->rangeBytes[0].len;
        }
        else if (req->numPart == 0)
            return -RS500;
    }
    else
    {
        req->respStatus = RS200;
        req->offset = 0;
        req->respContentLength = req->fileSize;
    }

    req->fd = open(req->path.ptr, O_RDONLY);
    if (req->fd == -1)
    {
        if (errno == EACCES)
            return -RS403;
        else if (errno == EMFILE)
        {
            print__err(req, "<%s:%d> Error open(%s): %s\n", __func__, __LINE__, req->path.ptr, strerror(errno));
            return -1;
        }
        else
            return -RS500;
    }

    if (req->numPart > 1)
    {
        int n = send_multypart(req);
            return n;
    }

    if (create_response_headers(req))
    {
        print__err(req, "<%s:%d>  Error send_header_response()\n", __func__, __LINE__);
        close(req->fd);
        return -1;
    }

    push_send_file(req);
    return 1;
}
//======================================================================
int send_multypart(Connect *r)
{
    long long send_all_bytes = 0;
    char buf[1024];
    StrReserve(&r->msg, 256);
    for (r->indPart = 0; r->indPart < r->numPart; r->indPart++)
    {
        send_all_bytes += (r->rangeBytes[r->indPart].len);
        send_all_bytes += create_multipart_head(r);
        if (r->msg.err)
            return -1;
    }

    send_all_bytes += snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    r->respContentLength = send_all_bytes;
    r->send_bytes = 0;

    StrReserve(&r->hdrs, 256);
    StrCpy(&r->hdrs, "Content-Type: multipart/byteranges; boundary=");
    StrCatLN(&r->hdrs, boundary);
    StrCat(&r->hdrs, "Content-Length: ");
    StrCatIntLN(&r->hdrs, send_all_bytes);
    if (r->hdrs.err)
    {
        print__err(r, "<%s:%d> Error create response headers\n", __func__, __LINE__);
        return -1;
    }

    r->indPart = 0;

    if (create_response_headers(r))
        return -1;
    push_send_multipart(r);
    return 1;
}
//======================================================================
int create_multipart_head(Connect *r)
{
    StrClear(&r->msg);
    StrCpy(&r->msg, "\r\n--");
    StrCatLN(&r->msg, boundary);

    if (r->respContentType)
    {
        StrCat(&r->msg, "Content-Type: ");
        StrCatLN(&r->msg, r->respContentType);
    }
    else
        return 0;
    StrCat(&r->msg, "Content-Range: bytes ");
    StrCatInt(&r->msg, r->rangeBytes[r->indPart].start);
    StrCat(&r->msg, "-");
    StrCatInt(&r->msg, r->rangeBytes[r->indPart].end);
    StrCat(&r->msg, "/");
    StrCatInt(&r->msg, r->fileSize);
    StrCat(&r->msg, "\r\n\r\n");

    return r->msg.len;
}
//======================================================================
int options(Connect *r)
{
    r->respStatus = RS200;
    r->respContentLength = 0;
    if (create_response_headers(r))
        return -1;
    StrFree(&r->html);
    push_send_html(r);
    return 1;
}
