#include "server.h"

extern fcgi_list_addr *fcgi_list;

#define FCGI_RESPONDER  1
//#define FCGI_AUTHORIZER 2
//#define FCGI_FILTER     3

//#define FCGI_KEEP_CONN  1

#define FCGI_VERSION_1           1

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

typedef struct {
    unsigned char type;
    int len;
    int padding_len;
} fcgi_header;

const int requestId = 1;

const int FCGI_SIZE_HEADER = 8;

void fcgi_set_header(char *header, int type, int id, int len, int padding_len);
//======================================================================
typedef struct {
    char *buf, *ptrPar;
    int sizeBuf;
    int lenBuf;
    int err;
    int numPar;
    int fcgi_sock;
} fcgi_param;
//======================================================================
fcgi_param init_fcgi_struct(int sock, char *buf, unsigned int size)
{
    fcgi_param tmp;
    tmp.buf = buf;
    tmp.ptrPar = buf + 8;
    tmp.sizeBuf = size - 8;
    tmp.lenBuf = tmp.err = tmp.numPar = 0;
    tmp.fcgi_sock = sock;
    return tmp;
}
//======================================================================
void send_par(fcgi_param *par, int end)
{
    if (par->err) return;
    unsigned char padding = 8 - (par->lenBuf % 8);
    padding = (padding == 8) ? 0 : padding;
    char *p = par->buf;
    *p++ = FCGI_VERSION_1;
    *p++ = FCGI_PARAMS;
    *p++ = (unsigned char) ((1 >> 8) & 0xff);
    *p++ = (unsigned char) ((1) & 0xff);
    
    *p++ = (unsigned char) ((par->lenBuf >> 8) & 0xff);
    *p++ = (unsigned char) ((par->lenBuf) & 0xff);
    
    *p++ = padding;
    *p = 0;
    
    memset(par->ptrPar + par->lenBuf, 0, padding);
    par->lenBuf += padding;
    
    if ((end) && ((par->lenBuf + 8) <= par->sizeBuf))
    {
        char s[8] = {1, 4, 0, 1, 0, 0, 0, 0};
        memcpy(par->ptrPar + par->lenBuf, s, 8);
        par->lenBuf += 8;
        end = 0;
    }
    
    int n = write_timeout(par->fcgi_sock, par->buf, 8 + par->lenBuf, conf->TimeoutCGI);
    if (n == -1)
    {
        par->err = 1;
        return;
    }
    
    par->lenBuf = 0;
    if (end)
        send_par(par, 0);
}
//======================================================================
void add_param(fcgi_param *par, const char *name, const char *val)
{
    if (par->err) return;
    par->numPar++;
    if (!name)
    {
        send_par(par, 1);
        return;
    }
    
    unsigned int len_name = strlen(name), len_val, len;
    
    if (val)
        len_val = strlen(val);
    else
        len_val = 0;
    
    len = len_name + len_val;
    len += len_name > 127 ? 4 : 1;
    len += len_val > 127 ? 4 : 1;
    
    if ((par->lenBuf + len + 8) > par->sizeBuf)
    {
        send_par(par, 0);
        if (par->err)
            return;
        if ((len + 8) > par->sizeBuf)
        {
            par->err = 1;
            return;
        }
    }
    
    char *p = par->ptrPar + par->lenBuf;
    if (len_name < 0x80)
        *p++ = (unsigned char)len_name;
    else
    {
        *p++ = (unsigned char)((len_name >> 24) | 0x80);
        *p++ = (unsigned char)(len_name >> 16);
        *p++ = (unsigned char)(len_name >> 8);
        *p++ = (unsigned char)len_name;
    }
    
    if (len_val < 0x80)
        *p++ = (unsigned char)len_val;
    else
    {
        *p++ = (unsigned char)((len_val >> 24) | 0x80);
        *p++ = (unsigned char)(len_val >> 16);
        *p++ = (unsigned char)(len_val >> 8);
        *p++ = (unsigned char)len_val;
    }
    
    memcpy(p, name, len_name);
    if (len_val > 0)
        memcpy(p + len_name, val, len_val);

    par->lenBuf += len;
}
//======================================================================
int get_sock_fcgi(const char *script)
{
    int fcgi_sock = -1, len;
    fcgi_list_addr *ps = conf->fcgi_list;
    
    if (!script)
    {
        print_err("<%s:%d> Not found\n", __func__, __LINE__);
        return -RS404;
    }

    len = strlen(script);
    if (len > 64)
    {
        print_err("<%s:%d> Error: name script too large\n", __func__, __LINE__);
        return -RS400;
    }

    for (; ps; ps = ps->next)
    {
        if (!strcmp(script, ps->scrpt_name))
            break;
    }

    if (ps != NULL)
    {
        fcgi_sock = create_client_socket(ps->addr);
        if (fcgi_sock < 0)
        {
            print_err("<%s:%d> Error create_client_socket(%s): %s\n", __func__, __LINE__, ps->addr, str_err(-fcgi_sock));
            fcgi_sock = -RS500;
        }
    }
    else
    {
        print_err("<%s:%d> Not found: %s\n", __func__, __LINE__, script);
        fcgi_sock = -RS404;
    }

    return fcgi_sock;
}
//======================================================================
void fcgi_set_header(char *header, int type, int id, int len, int padding_len)
{
    char *p = header;
    *p++ = FCGI_VERSION_1;                      // Protocol Version
    *p++ = type;                                // PDU Type
    *p++ = (unsigned char) ((id >> 8) & 0xff);  // Request Id
    *p++ = (unsigned char) ((id) & 0xff);       // Request Id
    
    *p++ = (unsigned char) ((len >> 8) & 0xff); // Content Length
    *p++ = (unsigned char) ((len) & 0xff);      // Content Length
    
    *p++ = padding_len;                         // Padding Length
    *p = 0;                                     // Reserved
}
//======================================================================
int tail_to_fcgi(int fcgi_sock, char *tail, int lenTail)
{
    int rd, wr, all_wr = 0;
    const int size_buf = 8192;
    char buf[16 + size_buf], *p = tail;

    while(lenTail > 0)
    {
        if (lenTail > size_buf)
            rd = size_buf;
        else
            rd = lenTail;
        memcpy(buf + 8, p, rd);
        
        size_t padding = 8 - (rd % 8);
        padding = (padding == 8) ? 0 : padding;
        fcgi_set_header(buf, FCGI_STDIN, requestId, rd, padding);
        
        wr = write_timeout(fcgi_sock, buf, rd + 8 + padding, conf->TimeoutCGI);
        if ((wr == -1) || ((rd + 8 + (int)padding) != wr))
        {
            return -1;
        }
        lenTail -= rd;
        all_wr += rd;
        p += rd;
    }
    return all_wr;
}
//======================================================================
int client_to_fcgi(int sock_cl, int fcgi_sock, int contentLength)
{
    int rd, wr;
    const int size_buf = 4096;
    char buf[16 + size_buf];
    
    while(contentLength > 0)
    {
        if (contentLength > size_buf)
            rd = read_timeout(sock_cl, buf + 8, size_buf, conf->TimeOut);
        else
            rd = read_timeout(sock_cl, buf + 8, contentLength, conf->TimeOut);
        if (rd <= 0)
        {
            return -1;
        }
        
        size_t padding = 8 - (rd % 8);
        padding = (padding == 8) ? 0 : padding;
        fcgi_set_header(buf, FCGI_STDIN, requestId, rd, padding);
        
        wr = write_timeout(fcgi_sock, buf, rd + 8 + padding, conf->TimeoutCGI);
        if (wr == -1)
        {
            return -1;
        }
        contentLength -= rd;
    }
    return 0;
}
//======================================================================
int fcgi_get_header(int fcgi_sock, fcgi_header *header)
{
    int n;
    char buf[8];
    
    n = read_timeout(fcgi_sock, buf, 8, conf->TimeoutCGI);
    if (n <= 0)
        return n;
    
    header->type = (unsigned char)buf[1];
    header->padding_len = (unsigned char)buf[6];
    header->len = ((unsigned char)buf[4]<<8) | (unsigned char)buf[5];
    
    return n;
}
//======================================================================
int fcgi_chunk(Connect *req, String *hdrs, int fcgi_sock, fcgi_header *header, char *tail_ptr, int tail_len)
{
    int n;
    int chunk_mode;
    if (req->reqMethod == M_HEAD)
        chunk_mode = NO_SEND;
    else
        chunk_mode = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;
    
    chunked chk = {MAX_LEN_SIZE_CHUNK, chunk_mode, 0, req->clientSocket};
    
    req->numPart = 0;

    if (chunk_mode == SEND_CHUNK)
    {
        str_cat(hdrs, "Transfer-Encoding: chunked\r\n");
    }
    
    if (chunk_mode)
    {
        if (send_response_headers(req, hdrs))
        {
            return -1;
        }
        
        if (req->respStatus == RS204)
        {
            return 0;
        }
    }
    //--------------------------- send entity --------------------------
    if ((tail_len > 0) && tail_ptr)
    {
        chunk_add_arr(&chk, tail_ptr, tail_len);
        if (chk.err)
        {
            print__err(req, "<%s:%d> Error chunk_buf.add_arr()\n", __func__, __LINE__);
            return -RS500;
        }
    }
    
    n = fcgi_to_client(&chk, fcgi_sock, header->len);
    if (n < 0)
    {
        print_err("<%s:%d> Error fcgi_to_client()=%d\n", __func__, __LINE__, n);
        return -1;
    }
    
    if (header->padding_len > 0)
    {
        n = fcgi_read_padding(fcgi_sock, header->padding_len, conf->TimeoutCGI);
        if (n < 0)
        {
            print_err("<%s:%d> read_timeout()=%d\n", __func__, __LINE__, n);
            return -1;
        }
        header->padding_len -= n;
    }
    //--------------------------- send entity --------------------------
    while(1)
    {
        n = fcgi_get_header(fcgi_sock, header);
        if (n < 0)
        {
            return -RS502;
        }

        if (header->type == FCGI_END_REQUEST)
        {
            n = fcgi_read_padding(fcgi_sock, header->len + header->padding_len, conf->TimeoutCGI);
            if (n < 0)
            {
                print_err("<%s:%d> read_timeout()=%d\n", __func__, __LINE__, n);
                return -1;
            }
            break;
        }
        
        if (header->type != FCGI_STDOUT)
        {
            print_err("<%s:%d> Error fcgi: type=%hhu\n", __func__, __LINE__, header->type);
            return -1;
        }

        n = fcgi_to_client(&chk, fcgi_sock, header->len);
        if (n < 0)
        {
            print_err("<%s:%d> Error fcgi_to_client()=%d\n", __func__, __LINE__, n);
            return -1;
        }
        
        if (header->padding_len > 0)
        {
            n = fcgi_read_padding(fcgi_sock, header->padding_len, conf->TimeoutCGI);
            if (n == -1)
            {
                print_err("<%s:%d> read_timeout()=%d\n", __func__, __LINE__, n);
                return -1;
            }
            header->padding_len -= n;
        }
    }
    //------------------------------------------------------------------
    chunk_end(&chk);
    req->respContentLength = chk.allSend;
    if (chk.err)
    {
        print_err("<%s:%d> Error chunk_end()\n", __func__, __LINE__);
        return -1;
    }
    
    if (chunk_mode == NO_SEND)
    {
        if (send_response_headers(req, hdrs))
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }
    }
    else
        req->send_bytes = req->respContentLength;
    
    return 0;
}
//======================================================================
int fcgi_read_headers(Connect *req, String *hdrs, int fcgi_sock)
{
    int n;
    fcgi_header header;
    
    req->respStatus = RS200;
    
    const char *err_str = NULL;
    while (1)
    {
        n = fcgi_get_header(fcgi_sock, &header);
        if (n <= 0)
        {
            err_str = "Error: fcgi_get_header()";
            break;
        }
        
        if (header.type == FCGI_STDOUT)
            break;
        else if (header.type == FCGI_STDERR)
        {
            n = fcgi_read_stderr(fcgi_sock, header.len, conf->TimeoutCGI);
            if (n <= 0)
            {
                err_str = "Error: fcgi_read_stderr()";
                break;
            }
            
            header.len -= n;
            
            if (header.padding_len > 0)
            {
                n = fcgi_read_padding(fcgi_sock, header.padding_len, conf->TimeoutCGI);
                if (n <= 0)
                {
                    err_str = "Error: fcgi_read_padding()";
                    break;
                }
            }
        }
        else
        {
            err_str = "Error header type";
            break;
        }
    }

    if (err_str)
    {
        print__err(req, "<%s:%d> \"%s\"\n", __func__, __LINE__, err_str);
        return -RS500;
    }
    
    if (header.type != FCGI_STDOUT)
    {
        print__err(req, "<%s:%d> Error: %hhu\n", __func__, __LINE__, header.type);
        return -RS500;
    }
    //-------------------------- read headers --------------------------
    const int size = 41;
    char buf[size];
    
    char *start_ptr = buf;
    err_str = "Error: Blank line not found";
    int i = 0;

    while (1)
    {
        int len;
        char *end_ptr, s[64];

        if (i > 0)
            end_ptr = (char*)memchr(start_ptr, '\n', i);
        else
            end_ptr = NULL;
        if(!end_ptr)
        {
            if (header.len == 0)
                break;
                
            if (i > 0)
                memmove(buf, start_ptr, i);
            int rd = size - i;
            if (rd <= 0)
            {
                print__err(req, "<%s:%d> Error: size()=%d, i=%d\n", __func__, __LINE__, size, i);
                err_str = "Error: Buffer for read is small";
                break;
            }
            
            if (header.len < rd)
                rd = header.len;

            int ret = read_timeout(fcgi_sock, buf + i, rd, conf->TimeoutCGI);
            if (ret <= 0)
            {
                print__err(req, "<%s:%d> read_from_script()=%d, read_len=%d\n", __func__, __LINE__, ret, rd);
                err_str = "Error: Read from script";
                break;
            }
            
            header.len -= ret;
            i += ret;
            start_ptr = buf;
            continue;
        }
        
        len = end_ptr - start_ptr;
        i = i - (len + 1);
        
        if (len > 0)
        {
            if (*(end_ptr - 1) == '\r')
                --len;
            memcpy(s, start_ptr, len);
            s[len] = '\0';
        }

        start_ptr = end_ptr + 1;

        if(len == 0)
        {
            err_str = NULL;
            break;
        }
        
        char *p;
        if((p = (char*)memchr(s, ':', len)))
        {
            if(!strlcmp_case(s, "Status", 6))
            {
                req->respStatus = strtol(++p, NULL, 10);
                if (req->respStatus >= RS500)
                {
                    send_message(req, NULL, NULL);
                    return 0;
                }

                continue;
            }
            else if(!strlcmp_case(s, "Date", 4) || \
                !strlcmp_case(s, "Server", 6) || \
                !strlcmp_case(s, "Accept-Ranges", 13) || \
                !strlcmp_case(s, "Content-Length", 14) || \
                !strlcmp_case(s, "Connection", 10))
            {
                print_err("<%s:%d> %s\n", __func__, __LINE__, s);
                continue;
            }
            
            str_cat_ln(hdrs, s);
            if (hdrs->err)
            {
                err_str = "Error: Create header";
                break;
            }
        }
        else
        {
            err_str = "Error: Line not header";
            break;
        }
    }

    if (err_str)
    {
        print__err(req, "<%s:%d> \"%s\" header.len=%d\n", __func__, __LINE__, err_str, header.len);
        return -1;
    }

    return fcgi_chunk(req, hdrs, fcgi_sock, &header, start_ptr, i);
}
//======================================================================
int send_param(Connect *req, int fcgi_sock)
{
    int ret = 0;
    char buf[4096];
    //------------------------- param ----------------------------------
    fcgi_set_header(buf, FCGI_BEGIN_REQUEST, requestId, 8, 0);
    
    buf[8] = (unsigned char) ((FCGI_RESPONDER >>  8) & 0xff);
    buf[9] = (unsigned char) (FCGI_RESPONDER         & 0xff);
    buf[10]=0;//(unsigned char) ((0) ? FCGI_KEEP_CONN : 0);
    buf[11]=0;
    buf[12]=0;
    buf[13]=0;
    buf[14]=0;
    buf[15]=0;
    
    ret = write_timeout(fcgi_sock, buf, 16, conf->TimeoutCGI);
    if (ret == -1)
    {
        print_err("<%s:%d> Error write_timeout(): %s\n", __func__, __LINE__, strerror(errno));
        return -RS502;
    }
    
    fcgi_param par = init_fcgi_struct(fcgi_sock, buf, sizeof(buf));
    
    add_param(&par, "PATH", "/bin:/usr/bin:/usr/local/bin");
    add_param(&par, "SERVER_SOFTWARE", conf->ServerSoftware);
    add_param(&par, "GATEWAY_INTERFACE", "CGI/1.1");
    add_param(&par, "DOCUMENT_ROOT", conf->DocumentRoot);
    add_param(&par, "REMOTE_ADDR", req->remoteAddr);
    add_param(&par, "REQUEST_URI", req->uri);
    
    if (req->reqMethod == M_HEAD)
        add_param(&par, "REQUEST_METHOD", get_str_method(M_GET));
    else
        add_param(&par, "REQUEST_METHOD", get_str_method(req->reqMethod));
    
    add_param(&par, "SERVER_PROTOCOL", get_str_http_prot(req->httpProt));
    
    if(req->req_hd.iHost >= 0)
        add_param(&par, "HTTP_HOST", req->reqHeadersValue[req->req_hd.iHost]);
    
    if(req->req_hd.iReferer >= 0)
        add_param(&par, "HTTP_REFERER", req->reqHeadersValue[req->req_hd.iReferer]);
    
    if(req->req_hd.iUserAgent >= 0)
        add_param(&par, "HTTP_USER_AGENT", req->reqHeadersValue[req->req_hd.iUserAgent]);
    
    add_param(&par, "SCRIPT_NAME", req->decodeUri);
    
    if (req->scriptType == php_fpm)
        add_param(&par, "SCRIPT_FILENAME", str_ptr(&req->path));
    
    if(req->reqMethod == M_POST)
    {
        if(req->req_hd.iReqContentType >= 0)
            add_param(&par, "CONTENT_TYPE", req->reqHeadersValue[req->req_hd.iReqContentType]);
        
        if(req->req_hd.iContentLength >= 0)
            add_param(&par, "CONTENT_LENGTH", req->reqHeadersValue[req->req_hd.iContentLength]);
    }
    
    add_param(&par, "QUERY_STRING", req->sReqParam);
    add_param(&par, NULL, NULL);
    if (par.err)
    {
        print_err("<%s:%d> Error send_param\n", __func__, __LINE__);
        return -RS500;
    }
    
    if(req->reqMethod == M_POST)
    {
        if (req->tail)
        {
            ret = tail_to_fcgi(fcgi_sock, req->tail, req->lenTail);
            if(ret < 0)
            {
                print__err(req, "<%s:%d> Error tail to script: %d\n", __func__, __LINE__, ret);
                return -RS500;
            }
            req->req_hd.reqContentLength -= ret;
        }
        
        ret = client_to_fcgi(req->clientSocket, fcgi_sock, req->req_hd.reqContentLength);
        if (ret == -1)
        {
            print_err("<%s:%d> Error client_to_fcgi()\n", __func__, __LINE__);
            return -RS500;
        }
    }
    
    fcgi_set_header(buf, FCGI_STDIN, requestId, 0, 0);
    ret = write_timeout(fcgi_sock, buf, 8, conf->TimeoutCGI);
    if (ret < 0)
    {
        print__err(req, "<%s:%d> Error client_to_fcgi()\n", __func__, __LINE__);
        return -RS500;
    }

    String hdrs = str_init(256);
    if (hdrs.err == 0)
    {
        ret = fcgi_read_headers(req, &hdrs, fcgi_sock);
        str_free(&hdrs);
        if (ret < 0)
        {
            req->connKeepAlive = 0;
            return ret;
        }
    }
    else
    {
        print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
        return -1;
    }
    
    return 0;
}
//======================================================================
int fcgi(Connect *req)
{
    int sock_fcgi = 0, ret = 0;

    if(req->reqMethod == M_POST)
    {
        if (req->req_hd.iReqContentType < 0)
        {
            print_err("<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if(req->req_hd.reqContentLength < 0)
        {
            print_err("<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
            req->req_hd.reqContentLength = 0;
        }

        if(req->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print_err("<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, req->req_hd.reqContentLength);
            return -RS413;
        }
    }
    
    if (timedwait_close_cgi())
    {
        req->connKeepAlive = 0;
        return -1;
    }
    
    if (req->scriptType == php_fpm)
    {
        sock_fcgi = create_client_socket(conf->PathPHP);
    }
    else if (req->scriptType == fast_cgi)
    {
        sock_fcgi = get_sock_fcgi(str_ptr(&req->scriptName));
    }
    else
    {
        print_err("<%s:%d> req->scriptType ?\n", __func__, __LINE__);
        ret = -RS500;
        goto err_exit;
    }
    
    if (sock_fcgi <= 0)
    {
        print_err("<%s:%d> Error connect to fcgi\n", __func__, __LINE__);
        if (sock_fcgi == 0)
            ret = -RS400;
        else
            ret = -RS404;
        goto err_exit;
    }
    
    ret = send_param(req, sock_fcgi);

    close(sock_fcgi);
    
err_exit:
    cgi_dec();
    if (ret < 0)
        req->connKeepAlive = 0;
    return ret;
}
