#include "server.h"

//======================================================================
extern struct pollfd *cgi_poll_fd;

int get_sock_fcgi(Connect *req, const char *script);
void cgi_del_from_list(Connect *r);
int scgi_set_param(Connect *r);
int cgi_set_size_chunk(Connect *r);
int write_to_fcgi(Connect* r);
int cgi_read_http_headers(Connect *req);
int cgi_stdin(Connect *req);
int cgi_stdout(Connect *req);

void add_param(Connect *r, const char *name, const char *val);
//======================================================================
int scgi_set_size_data(Connect* r)
{
    int size = r->cgi.len_buf;
    int i = 7;
    char *p = r->cgi_buf;
    p[i--] = ':';
    
    for ( ; i >= 0; --i)
    {
        p[i] = (size % 10) + '0';
        size /= 10;
        if (size == 0)
            break;
    }
    
    if (size != 0)
        return -1;

    r->cgi_buf[8 + r->cgi.len_buf] = ',';
    r->cgi.p = r->cgi_buf + i;
    r->cgi.len_buf += (8 - i + 1);

    return 0;
}
//======================================================================
int scgi_create_connect(Connect *r)
{
    r->cgi.op.scgi = SCGI_CONNECT;
    r->cgi.dir = TO_CGI;
    if (r->reqMethod == M_POST)
    {
        if (r->req_hd.iReqContentType < 0)
        {
            print__err(r, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if (r->req_hd.reqContentLength < 0)
        {
            print__err(r, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
        }

        if (r->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print__err(r, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, r->req_hd.reqContentLength);
            return -RS413;
        }
    }

    r->fcgi.fd = get_sock_fcgi(r, StrPtr(&r->scriptName));
    if (r->fcgi.fd < 0)
    {
        print__err(r, "<%s:%d> Error connect to scgi\n", __func__, __LINE__);
        return r->fcgi.fd;
    }

    add_param(r, "PATH", "/bin:/usr/bin:/usr/local/bin");
    add_param(r, "SERVER_SOFTWARE", conf->ServerSoftware);
    add_param(r, "SCGI", "1");
    add_param(r, "DOCUMENT_ROOT", conf->DocumentRoot);
    add_param(r, "REMOTE_ADDR", r->remoteAddr);
    //add_param(r, "REMOTE_PORT", r->remotePort);
    add_param(r, "REQUEST_URI", r->uri);
    add_param(r, "DOCUMENT_URI", r->decodeUri);

    if (r->reqMethod == M_HEAD)
        add_param(r, "REQUEST_METHOD", get_str_method(M_GET));
    else
        add_param(r, "REQUEST_METHOD", get_str_method(r->reqMethod));

    add_param(r, "SERVER_PROTOCOL", get_str_http_prot(r->httpProt));
    add_param(r, "SERVER_PORT", conf->ServerPort);

    if (r->req_hd.iHost >= 0)
        add_param(r, "HTTP_HOST", r->reqHeadersValue[r->req_hd.iHost]);

    if (r->req_hd.iReferer >= 0)
        add_param(r, "HTTP_REFERER", r->reqHeadersValue[r->req_hd.iReferer]);

    if (r->req_hd.iUserAgent >= 0)
        add_param(r, "HTTP_USER_AGENT", r->reqHeadersValue[r->req_hd.iUserAgent]);

    if (r->connKeepAlive == 1)
        add_param(r, "HTTP_CONNECTION", "keep-alive");
    else
        add_param(r, "HTTP_CONNECTION", "close");

    add_param(r, "SCRIPT_NAME", r->decodeUri);
    
    if (r->reqMethod == M_POST)
    {
        if (r->req_hd.iReqContentType >= 0)
            add_param(r, "CONTENT_TYPE", r->reqHeadersValue[r->req_hd.iReqContentType]);

        if (r->req_hd.iContentLength >= 0)
            add_param(r, "CONTENT_LENGTH", r->reqHeadersValue[r->req_hd.iContentLength]);
    }
    else
    {
        add_param(r, "CONTENT_LENGTH", "0");
        add_param(r, "CONTENT_TYPE", "");
    }

    if (r->sReqParam)
        add_param(r, "QUERY_STRING", r->sReqParam);
    else
        add_param(r, "QUERY_STRING", "");
    if (r->err)
    {
        print__err(r, "<%s:%d> Error create list params\n", __func__, __LINE__);
        return r->err;
    }

    r->fcgi.i_param = 0;
    
    r->cgi.op.scgi = SCGI_PARAMS;
    r->cgi.dir = TO_CGI;
    r->cgi.len_buf = 0;
    r->timeout = conf->TimeoutCGI;
    r->sock_timer = 0;
    
    int ret = scgi_set_param(r);
    if (ret <= 0)
    {
        fprintf(stderr, "<%s:%d> Error scgi_set_param()\n", __func__, __LINE__);
        print__err(r, "<%s:%d> Error scgi_set_param\n", __func__, __LINE__);
        return -RS502;
    }

    return 0;
}
//======================================================================
int scgi_set_param(Connect *r)
{
    r->cgi.len_buf = 0;
    r->cgi.p = r->cgi_buf + 8;

    for ( ; r->fcgi.i_param < r->fcgi.size_par; ++r->fcgi.i_param)
    {
        int len_name = strlen(r->fcgi.vPar[r->fcgi.i_param].name);
        if (len_name == 0)
        {
            print__err(r, "<%s:%d> Error: len_name=0\n", __func__, __LINE__);
            return -RS502;
        }

        int len_val = strlen(r->fcgi.vPar[r->fcgi.i_param].val);
        int len = len_name + len_val + 2;

        if (len > (CGI_BUF_SIZE - r->cgi.len_buf))
        {
            break;
        }

        memcpy(r->cgi.p, r->fcgi.vPar[r->fcgi.i_param].name, len_name);
        r->cgi.p += len_name;
        
        memcpy(r->cgi.p, "\0", 1);
        r->cgi.p += 1;

        if (len_val > 0)
        {
            memcpy(r->cgi.p, r->fcgi.vPar[r->fcgi.i_param].val, len_val);
            r->cgi.p += len_val;
        }

        memcpy(r->cgi.p, "\0", 1);
        r->cgi.p += 1;

        r->cgi.len_buf += len;
    }
    
    if (r->fcgi.i_param < r->fcgi.size_par)
    {
        print__err(r, "<%s:%d> Error: size of param > size of buf\n", __func__, __LINE__);
        return -RS502;
    }

    if (r->cgi.len_buf > 0)
    {      
        scgi_set_size_data(r);
    }
    else
    {
        print__err(r, "<%s:%d> Error: size param = 0\n", __func__, __LINE__);
        return -RS502;
    }

    return r->cgi.len_buf;
}
//======================================================================
void scgi_worker(Connect* r)
{
    if (r->cgi.op.scgi == SCGI_PARAMS)
    {
        int ret = write_to_fcgi(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }

            return;
        }

        r->sock_timer = 0;
        if (r->cgi.len_buf == 0)
        {
            if (r->req_hd.reqContentLength > 0)
            {
                r->cgi.len_post = r->req_hd.reqContentLength - r->lenTail;
                r->cgi.op.scgi = SCGI_STDIN;
                if (r->lenTail > 0)
                {
                    r->cgi.dir = TO_CGI;
                    r->cgi.p = r->tail;
                    r->cgi.len_buf = r->lenTail;
                    r->tail = NULL;
                    r->lenTail = 0;
                }
                else
                {
                    r->cgi.dir = FROM_CLIENT;
                }
            }
            else
            {
                r->cgi.op.scgi = SCGI_READ_HTTP_HEADERS;
                r->cgi.dir = FROM_CGI;
                r->tail = NULL;
                r->lenTail = 0;
                r->p_newline = r->cgi.p = r->cgi_buf + 8;
                r->cgi.len_buf = 0;
            }
        }
    }
    else if (r->cgi.op.scgi == SCGI_STDIN)
    {
        int ret = cgi_stdin(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }
        }
        else
            r->sock_timer = 0;
    }
    else //==================== SCGI_STDOUT=============================
    {
        if (r->cgi.op.scgi == SCGI_READ_HTTP_HEADERS)
        {
            int ret = cgi_read_http_headers(r);
            if (ret < 0)
            {
                if (ret == ERR_TRY_AGAIN)
                    r->io_status = POLL;
                else
                {
                    r->err = -RS502;
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else if (ret > 0)
            {
                r->mode_send = ((r->httpProt == HTTP11) && r->connKeepAlive) ? CHUNK : NO_CHUNK;
                if (create_response_headers(r))
                {
                    print__err(r, "<%s:%d> Error create_response_headers()\n", __func__, __LINE__);
                    r->err = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                }
                else
                {
                    r->cgi.op.scgi = SCGI_SEND_HTTP_HEADERS;
                    r->cgi.dir = TO_CLIENT;
                    r->sock_timer = 0;
                }
            }
            else // ret == 0
                r->sock_timer = 0;
        }
        else if (r->cgi.op.scgi == SCGI_SEND_HTTP_HEADERS)
        {
            if (r->resp_headers.len > 0)
            {
                int wr = write_to_client(r, r->resp_headers.ptr + r->resp_headers.ind, r->resp_headers.len - r->resp_headers.ind);
                if (wr < 0)
                {
                    if (wr == ERR_TRY_AGAIN)
                        r->io_status = POLL;
                    else
                    {
                        r->err = -1;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHeadersValue[r->req_hd.iReferer] = "Connection reset by peer";
                        cgi_del_from_list(r);
                        end_response(r);
                    }
                }
                else
                {
                    r->resp_headers.ind += wr;
                    if ((r->resp_headers.len - r->resp_headers.ind) == 0)
                    {
                        if (r->reqMethod == M_HEAD)
                        {
                            cgi_del_from_list(r);
                            end_response(r);
                        }
                        else
                        {
                            r->cgi.op.scgi = SCGI_SEND_ENTITY;
                            r->sock_timer = 0;
                            if (r->lenTail > 0)
                            {
                                r->cgi.p = r->tail;
                                r->cgi.len_buf = r->lenTail;
                                r->tail = NULL;
                                r->lenTail = 0;
                                r->cgi.dir = TO_CLIENT;
                                if (r->mode_send == CHUNK)
                                {
                                    if (cgi_set_size_chunk(r))
                                    {
                                        r->err = -1;
                                        cgi_del_from_list(r);
                                        end_response(r);
                                    }
                                }
                            }
                            else
                            {
                                r->cgi.len_buf = 0;
                                r->cgi.p = NULL;
                                r->cgi.dir = FROM_CGI;
                            }
                        }
                    }
                    else
                        r->sock_timer = 0;
                }
            }
            else
            {
                print__err(r, "<%s:%d> Error resp.len=%d\n", __func__, __LINE__, r->resp_headers.len);
                r->err = -1;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHeadersValue[r->req_hd.iReferer] = "Error send response headers";
                cgi_del_from_list(r);
                end_response(r);
            }
        }
        else if (r->cgi.op.scgi == SCGI_SEND_ENTITY)
        {
            int ret = cgi_stdout(r);
            if (ret < 0)
            {
                if (ret == ERR_TRY_AGAIN)
                    r->io_status = POLL;
                else
                {
                    r->err = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else if (ret == 0) // end SCGI_SEND_ENTITY
            {
                cgi_del_from_list(r);
                end_response(r);
            }
            else
                r->sock_timer = 0;
        }
        else
        {
            print__err(r, "<%s:%d> ??? Error: SCGI_OPERATION=%s\n", __func__, __LINE__, get_scgi_operation(r->cgi.op.scgi));
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
}
//======================================================================
int timeout_scgi(Connect *r)
{
    if (((r->cgi.op.scgi == SCGI_PARAMS) || (r->cgi.op.scgi == SCGI_STDIN)) && 
         (r->cgi.dir == TO_CGI))
        return -RS504;
    else if (r->cgi.op.scgi == SCGI_READ_HTTP_HEADERS)
        return -RS504;
    else
        return -1;
}
