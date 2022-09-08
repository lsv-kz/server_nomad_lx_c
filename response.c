#include "server.h"

int response2(Connect *req);
//======================================================================
void response1(int num_chld)
{
    int n;
    int lenRootDir, lenCgiDir;
    const char *p;
    Connect *req;

    while(1)
    {
        req = pop_resp_list();
        if (!req)
        {
            end_thr(1);
            return;
        }
        else if (req->clientSocket < 0)
        {
            end_thr(1);
            free_req(req);
            return;
        }
        //--------------------------------------------------------------
        int ret = parse_startline_request(req, req->reqHeadersName[0]);
        if (ret)
        {
            print__err(req, "<%s:%d>  Error parse_startline_request(): %d\n", __func__, __LINE__, ret);
            goto end;
        }
     
        for (int i = 1; i < req->countReqHeaders; ++i)
        {
            ret = parse_headers(req, req->reqHeadersName[i], i);
            if (ret < 0)
            {
                print__err(req, "<%s:%d>  Error parse_headers(): %d\n", __func__, __LINE__, ret);
                goto end;
            }
        }
        //--------------------------------------------------------------
    #ifdef TCP_CORK_
        if (conf->tcp_cork == 'y')
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

        if (req->numReq >= conf->MAX_REQUESTS_PER_THR || (conf->KEEP_ALIVE == 'n') || (req->httpProt == HTTP10))
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
        clean_path(req->decodeUri);
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
            lenRootDir = strlen(conf->ROOTDIR);
            lenCgiDir = strlen(conf->CGIDIR);
            if ((lenCgiDir - lenRootDir) < 0)
                req->sizePath = lenRootDir + req->lenDecodeUri + 256 + 1;
            else
                req->sizePath = lenCgiDir + req->lenDecodeUri + 256 + 1;

            str_cpy(&req->path, conf->ROOTDIR);
            str_cat(&req->path, req->decodeUri);
            if (req->path.err == 0)
            {
                int ret = response2(req);
                free_range(req);
                if (ret == 1)
                {// "req" may be free
                    int ret = end_thr(0);
                    if (ret == EXIT_THR)
                        return;
                    else
                        continue;
                }
                else
                    req->sizePath = 0;
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
        if (req->err <= -RS101)
        {
            if ((req->reqMethod == M_POST) || (req->reqMethod == M_PUT))
                req->connKeepAlive = 0;
        }

        end_response(req);

        n = end_thr(0);
        if (n)
        {
            return;
        }
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
int send_multypart(Connect *req, String *hdrs, char *rd_buf, int *size);
int read_dir(Connect *req);
int get_ranges(Connect *req);
int create_multipart_head(Connect *req, Range *ranges, char *buf, int len_buf);
const char boundary[] = "---------a9b5r7a4c0a2d5a1b8r3a";
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
        if (i->scrpt_name[0] == '~')
        {
            if (!strcmp(p, i->scrpt_name + 1))
                break;
        }
        else
        {
            if (!strcmp(uri, i->scrpt_name))
                break;
        }
    }

    if (!i)
        return -RS404;
    req->scriptType = fast_cgi;
    str_cpy(&req->scriptName, i->scrpt_name);

    return fcgi(req);
}
//======================================================================
int response2(Connect *req)
{
    int n;
    struct stat st;
    char *p = strstr(req->decodeUri, ".php");
    if (p && (*(p + 4) == 0))
    {
        int ret;
        if (strcmp(conf->UsePHP, "php-cgi") && strcmp(conf->UsePHP, "php-fpm"))
        {
            print_err("<%s:%d> Not found: %s\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        if (stat(req->decodeUri + 1, &st) == -1)
        {
            print_err("<%s:%d> script (%s) not found\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        if (!strcmp(conf->UsePHP, "php-fpm"))
        {
            req->scriptType = php_fpm;
            ret = fcgi(req);
        }
        else if (!strcmp(conf->UsePHP, "php-cgi"))
        {
            req->scriptType = php_cgi;
            ret = cgi(req);
        }
        else
        {
            print_err("<%s:%d> PHP not implemented\n", __func__, __LINE__);
            ret = -RS500;
        }

        return ret;
    }

    if (!strncmp(req->decodeUri, "/cgi-bin/", 9) 
        || !strncmp(req->decodeUri, "/cgi/", 5))
    {
        req->scriptType = cgi_ex;
        return cgi(req);
    }
    //------------------------------------------------------------------
    if (lstat(str_ptr(&req->path), &st) == -1)
    {
        if (errno == EACCES)
            return -RS403;
        return fastcgi(req, req->decodeUri);
    }
    else
    {
        if ((!S_ISDIR(st.st_mode)) && (!S_ISREG(st.st_mode)))
        {
            print_err("<%s:%d> Error: file (!S_ISDIR && !S_ISREG) \n", __func__, __LINE__);
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

            String hdrs = str_init(200); // str_free(&hdrs);
            str_cpy(&hdrs, "Location: ");
            str_cat_ln(&hdrs, req->uri);
            if (hdrs.err == 0)
            {
                String s = str_init(256); // str_free(&s);
                str_cpy(&s, "The document has moved <a href=\"");
                str_cat(&s, req->uri);
                str_cat(&s, "\">here</a>.");
                if (s.err == 0)
                {
                    send_message(req, &hdrs, str_ptr(&s));
                }
                else
                    print_err("<%s:%d> Error: malloc()\n", __func__, __LINE__);
                str_free(&s);
            }
            else
                print_err("<%s:%d> Error: malloc()\n", __func__, __LINE__);

            str_free(&hdrs);
            return 0;
        }
        //..............................................................
        int lenPath = str_len(&req->path);
        str_cat(&req->path, "index.html");
        if (req->path.err)
        {
            print_err("<%s:%d> ---\n", __func__, __LINE__);
            return -RS500;
        }

        if ((stat(str_ptr(&req->path), &st) != 0) || (conf->index_html != 'y'))
        {
            errno = 0;

            if (strcmp(conf->UsePHP, "n") && (conf->index_php == 'y'))
            {
                str_resize(&req->path, lenPath);
                const char *NameScript = "index.php";
                str_cat(&req->path, "index.php");
                if (!stat(str_ptr(&req->path), &st))
                {
                    str_reserve(&req->scriptName, strlen(NameScript) + 1);
                    str_cpy(&req->scriptName, req->decodeUri);
                    str_cat(&req->scriptName, NameScript);
                    if (req->scriptName.err == 0)
                    {
                        if (!strcmp(conf->UsePHP, "php-fpm"))
                        {
                            req->scriptType = php_fpm;
                            return fcgi(req);
                        }
                        else if (!strcmp(conf->UsePHP, "php-cgi"))
                        {
                            req->scriptType = php_cgi;
                            return cgi(req);
                        }
                        else
                        {
                            print__err(req, "<%s:%d> Error UsePHP: %s\n", __func__, __LINE__, conf->UsePHP);
                            return -1;
                        }
                    }
                }
            }

            str_resize(&req->path, lenPath);

            if (conf->index_pl == 'y')
            {
                req->scriptType = cgi_ex;
                str_cpy(&req->scriptName, "/cgi-bin/index.pl");
                return cgi(req);
            }
            else if (conf->index_fcgi == 'y')
            {
                req->scriptType = fast_cgi;
                str_cpy(&req->scriptName, "/index.fcgi");
                return fcgi(req);
            }

            return read_dir(req);
        }
    }

    if (req->reqMethod == M_POST)
        return -RS405;
    //----------------------- send file --------------------------------
    req->fileSize = file_size(str_ptr(&req->path));
    req->numPart = 0;
    req->respContentType = content_type(str_ptr(&req->path));

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
            if (req->reqMethod == M_HEAD)
            {
                if (send_response_headers(req, NULL))
                    return -1;
                return 0;
            }
        }
        else if (req->numPart == 0)
            return -RS500;
    }
    else
    {
        req->respStatus = RS200;
        req->offset = 0;
        req->respContentLength = req->fileSize;
        if (req->reqMethod == M_HEAD)
        {
            if (send_response_headers(req, NULL))
                return -1;
            return 0;
        }
    }

    req->fd = open(str_ptr(&req->path), O_RDONLY);
    if (req->fd == -1)
    {
        if (errno == EACCES)
            return -RS403;
        else if (errno == EMFILE)
        {
            print_err("<%s:%d> Error open(%s): %s\n", __func__, __LINE__, str_ptr(&req->path), strerror(errno));
            return -1;
        }
        else
            return -RS500;
    }

    if (req->numPart > 1)
    {
        int size_buf = conf->SNDBUF_SIZE;
        char *rd_buf = malloc(size_buf);
        if (!rd_buf)
        {
            print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            close(req->fd);
            return -1;
        }
        String hdrs = str_init(256);
        if (hdrs.err == 0)
        {
            n = send_multypart(req, &hdrs, rd_buf, &size_buf);
            str_free(&hdrs);
        }
        else
            n = -1;
        free(rd_buf);
        close(req->fd);
        return n;
    }
    
    if (send_response_headers(req, NULL))
    {
        print_err("<%s:%d>  Error send_header_response()\n", __func__, __LINE__);
        close(req->fd);
        return -1;
    }
    
    if (req->reqMethod == M_HEAD)
    {
        close(req->fd);
        return 0;
    }
    
    push_pollout_list(req);
    return 1;
}
//======================================================================
int send_multypart(Connect *req, String *hdrs, char *rd_buf, int *size_buf)
{
    int n, i;
    long long send_all_bytes = 0, len;
    char buf[1024];
    Range *range;

    for (i = 0; i < req->numPart; i++)
    {
        send_all_bytes += (req->rangeBytes[i].len);
        send_all_bytes += create_multipart_head(req, &req->rangeBytes[i], buf, sizeof(buf));
    }
    send_all_bytes += snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    req->respContentLength = send_all_bytes;
    
    str_cat(hdrs, "Content-Type: multipart/byteranges; boundary=");
    str_cat_ln(hdrs, boundary);
    
    str_cat(hdrs, "Content-Length: ");
    str_llint_ln(hdrs, send_all_bytes);
    
    if (hdrs->err)
        return -1;
    
    if (send_response_headers(req, hdrs))
        return -1;
    
    if (req->reqMethod == M_HEAD)
        return 0;
    
    send_all_bytes = 0;
    
    for (i = 0; i < req->numPart; i++)
    {
        range = &req->rangeBytes[i];
        if ((n = create_multipart_head(req, range, buf, sizeof(buf))) == 0)
        {
            print_err("<%s:%d> Error create_multipart_head()=%d\n", __func__, __LINE__, n);
            return -1;
        } 

        n = write_timeout(req->clientSocket, buf, strlen(buf), conf->TIMEOUT);
        if (n < 0)
        {
            print_err("<%s:%d> Error: Sent %lld bytes\n", __func__, __LINE__, send_all_bytes);
            return -1;
        }

        len = range->len;
        n = send_file_ux(req->clientSocket, req->fd, rd_buf, size_buf, range->start, &range->len);
        if (n < 0)
        {
            print_err("<%s:%d> Error: Sent %lld bytes\n", __func__, __LINE__, 
                    send_all_bytes += (len - range->len));
            return -1;
        }
        else
            send_all_bytes += (len - range->len);
    }

    snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    n = write_timeout(req->clientSocket, buf, strlen(buf), conf->TIMEOUT);
    req->send_bytes = send_all_bytes + n;
    if (n < 0)
    {
        print_err("<%s:%d> Error: Sent %lld bytes\n", __func__, __LINE__, send_all_bytes);
        return -1;
    }

    return 0;
}
//======================================================================
int create_multipart_head(Connect *req, Range *ranges, char *buf, int len_buf)
{
    int n, all = 0;
    
    n = snprintf(buf, len_buf, "\r\n--%s\r\n", boundary);
    buf += n;
    len_buf -= n;
    all += n;

    if (req->respContentType && (len_buf > 0))
    {
        n = snprintf(buf, len_buf, "Content-Type: %s\r\n", req->respContentType);
        buf += n;
        len_buf -= n;
        all += n;
    }
    else
        return 0;
    
    if (len_buf > 0)
    {
        n = snprintf(buf, len_buf,
            "Content-Range: bytes %lld-%lld/%lld\r\n\r\n",
             ranges->start, ranges->end, req->fileSize);
        buf += n;
        len_buf -= n;
        all += n;
    }
    else
        return 0;

    return all;
}
//======================================================================
int options(Connect *req)
{
    req->respStatus = RS200;
    send_response_headers(req, NULL);
    return 0;
}
