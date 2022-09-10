#include "server.h"

extern int MaxChldsCgi;

int get_sock_fcgi(char *script);

pthread_mutex_t mtx_chld = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_close_cgi = PTHREAD_COND_INITIALIZER;

static int num_chlds = 0;
//======================================================================
int timedwait_close_cgi(void)
{
    int ret = 0;
pthread_mutex_lock(&mtx_chld);
    while (num_chlds >= conf->MaxProcCgi)
    {
        struct timeval now;
        struct timespec ts;
    
        gettimeofday(&now, NULL);
        ts.tv_sec = now.tv_sec + conf->TimeoutCGI;
        ts.tv_nsec = now.tv_usec * 1000;
    
        ret = pthread_cond_timedwait(&cond_close_cgi, &mtx_chld, &ts);
        if (ret == ETIMEDOUT)
        {
            print_err("<%s:%d> Timeout: %s\n", __func__, __LINE__, str_err(ret));
            ret = -RS503;
            break;
        }
        else if (ret > 0)
        {
            print_err("<%s:%d> Error pthread_cond_timedwait(): %s\n", __func__, __LINE__, str_err(ret));
            ret = -1;
            break;
        }
    }
    
    if (ret == 0)
        ++num_chlds;

pthread_mutex_unlock(&mtx_chld);
    return ret;
}
//======================================================================
void cgi_dec()
{
pthread_mutex_lock(&mtx_chld);
    --num_chlds;
pthread_mutex_unlock(&mtx_chld);
    pthread_cond_signal(&cond_close_cgi);
}
//======================================================================
const char *cgi_script_file(const char *name)
{
    char *p;

    if((p = strchr(name + 1, '/')))
    {
        return p;
    }
    return name;
}
//======================================================================
int wait_pid(Connect *req, int pid)
{
    int n, status, ret = 0;

    n = waitpid(pid, &status, WNOHANG);
    if(n == pid)
    {
        ret = 0;
    }
    else if (n == -1)
    {
        print__err(req, "<%s:%d> Error waitpid(%d): %s\n", __func__, __LINE__, pid, str_err(errno));
        ret = -1;
    }
    else
    {
        if(kill(pid, SIGKILL) == 0)
        {
            n = waitpid(pid, &status, 0); // WNOHANG
        }
        ret = 0;
    }

    return ret;
}
//======================================================================
int kill_script(Connect *req, int pid, int stat, const char *msg)
{
    int n, status;

    req->connKeepAlive = 0;
    if(stat > 0)
    {
        req->respStatus = stat;
        send_message(req, NULL, msg);
    }

    if((n = kill(pid, SIGKILL)) == 0)
    {
        n = waitpid(pid, &status, 0);
    }
    else
    {
        print__err(req, "<%s:%d> ! Error kill(%d): %s\n", __func__, __LINE__, pid, strerror(errno));
    }

    return -1;
}
//======================================================================
int cgi_chunk(Connect *req, String *hdrs, int cgi_serv_in, char *start_ptr, int ReadFromScript)
{
    int chunk;
    
    if (req->reqMethod == M_HEAD)
        chunk = NO_SEND;
    else
        chunk = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;
 
    chunked chk = {MAX_LEN_SIZE_CHUNK, chunk, 0, req->clientSocket, 0};

    req->numPart = 0;
    req->respContentType = NULL;
    req->respContentLength = -1;
    req->send_bytes = 0;

    if (req->reqMethod == M_HEAD)
    {
        int n = cgi_to_cosmos(cgi_serv_in, conf->TimeoutCGI);
        if (n < 0)
        {
            print__err(req, "<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }
        req->respContentLength = ReadFromScript + n;
        if (send_response_headers(req, hdrs))
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
        }
        return 0;
    }
        
    if (chunk == SEND_CHUNK)
    {
        str_cat(hdrs, "Transfer-Encoding: chunked\r\n");
    }
    
    if (send_response_headers(req, hdrs))
    {
        print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
        return -1;
    }
    //------------------ send entity to client -------------------------
    if (ReadFromScript > 0)
    {
        chunk_add_arr(&chk, start_ptr, ReadFromScript);
        if (chk.err)
        {
            print_err("<%s:%d> Error send to client\n", __func__, __LINE__);
            req->send_bytes = chk.allSend;
            return -1;
        }
    }

    ReadFromScript = cgi_to_client(&chk, cgi_serv_in);
    if (ReadFromScript < 0)
    {
        print_err("<%s:%d> Error cgi_to_client()=%d\n", __func__, __LINE__, ReadFromScript);
        req->send_bytes = chk.allSend;
        return -1;
    }

    chunk_end(&chk);
    req->send_bytes = chk.allSend;
    if (chk.err)
    {
        print_err("<%s:%d> Error chunk_end()\n", __func__, __LINE__);
        return -1;
    }
    
    return 0;
}
//======================================================================
int cgi_read_headers(Connect *req, String *hdrs, int cgi_serv_in)
{
    int ReadFromScript;
    const int size = 256;
    char buf[size];

    req->respStatus = RS200;
    ReadFromScript = read_timeout(cgi_serv_in, buf, size, conf->TimeoutCGI);
    if(ReadFromScript <= 0)
    {
        print__err(req, "<%s:%d> ReadFromScript=%d\n", __func__, __LINE__, ReadFromScript);
        return -RS500;
    }
    
    char *start_ptr = buf;
    for ( int line = 0; line < 10; ++line)
    {
        int len = 0;
        char *end_ptr, *str, *p;
        
        str = end_ptr = start_ptr;
        for ( ; ReadFromScript > 0; end_ptr++)
        {
            ReadFromScript--;
            if (*end_ptr == '\r')
                *end_ptr = 0;
            else if (*end_ptr == '\n')
                break;
            else
                len++;
        }
        
        if (*end_ptr != '\n')
        {
            print__err(req, "<%s:%d> Error: Blank line not found\n", __func__, __LINE__);
            return -RS500;
        }
        
        *end_ptr = 0;
        
        start_ptr = end_ptr + 1;

        if (len == 0)
            break;
        
        if (!(p = memchr(str, ':', len)))
        {
            print__err(req, "<%s:%d> Error: Line not header [%s]\n", __func__, __LINE__, str);
            return -RS500;
        }
        
        if (!strlcmp_case(str, "Status", 6))
        {
            req->respStatus = atoi(p + 1);//  respStatus = strtol(p + 1, NULL, 10);
            if(req->respStatus == RS204)
            {
                send_message(req, NULL, NULL);
                return 0;
            }
            continue;
        }
        
        else if (!strlcmp_case(str, "Date", 4) || \
                !strlcmp_case(str, "Server", 6) || \
                !strlcmp_case(str, "Accept-Ranges", 13) || \
                !strlcmp_case(str, "Content-Length", 14) || \
                !strlcmp_case(str, "Connection", 10))
        {
            print__err(req, "<%s:%d> %s\n", __func__, __LINE__, str);
            continue;
        }
            
        str_cat_ln(hdrs, str);
        if (hdrs->err)
        {
            print__err(req, "<%s:%d> Error create_header()\n", __func__, __LINE__);
            return -RS500;
        }
    }
    
    return cgi_chunk(req, hdrs, cgi_serv_in, start_ptr, ReadFromScript);
}
//======================================================================
int cgi_fork(Connect *req)
{
    int serv_cgi[2], cgi_serv[2];
    int wr_bytes, n;
    struct stat st;

    switch(req->scriptType)
    {
        case cgi_ex:
            str_cpy(&req->path, conf->ScriptPath);
            str_cat(&req->path, cgi_script_file(str_ptr(&req->scriptName)));
            break;
        case php_cgi:
            str_cpy(&req->path, conf->DocumentRoot);
            str_cat(&req->path, req->decodeUri);
            break;
        default:
            print__err(req, "<%s:%d> ScriptType \?(404)\n", __func__, __LINE__);
            return -1;
    }

    if (str_ptr(&req->path)[str_len(&req->path)] == '/')
        str_resize(&req->path, str_len(&req->path) - 1);

    if(stat(str_ptr(&req->path), &st) == -1)
    {
        print__err(req, "<%s:%d> script (%s) not found\n", __func__, __LINE__, str_ptr(&req->path));
        return -RS404;
    }

    n = pipe(serv_cgi);
    if (n < 0)
    {
        print__err(req, "<%s:%d> Error try_open_pipe()=%d; %d\n", __func__, __LINE__, n, req->clientSocket);
        return -1;
    }

    n = pipe(cgi_serv);
    if (n < 0)
    {
        print__err(req, "<%s:%d> Error try_open_pipe()=%d; %d\n", __func__, __LINE__, n, req->clientSocket);
        close(cgi_serv[0]);
        close(cgi_serv[1]);
        return -1;
    }
    //--------------------------- fork ---------------------------------
    pid_t pid = fork();
    if(pid < 0)
    {
        print__err(req, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, str_err(errno));
        close(serv_cgi[0]);
        close(serv_cgi[1]);
        close(cgi_serv[0]);
        close(cgi_serv[1]);
        req->connKeepAlive = 0;
        return -RS500;
    }
    else if(pid == 0)
    {
        char s[64];
        //----------------------- child --------------------------------
        close(cgi_serv[0]);
        close(serv_cgi[1]);
        
        if (serv_cgi[0] != STDIN_FILENO)
        {
            if (dup2(serv_cgi[0], STDIN_FILENO) < 0)
                goto err_child;
            if (close(serv_cgi[0]) < 0)
                goto err_child;
        }

        if (cgi_serv[1] != STDOUT_FILENO)
        {
            if (dup2(cgi_serv[1], STDOUT_FILENO) < 0)
                goto err_child;
            if (close(cgi_serv[1]) < 0)
                goto err_child;
        }

        if(req->scriptType == php_cgi)
            setenv("REDIRECT_STATUS", "true", 1);
        setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("SERVER_SOFTWARE", conf->ServerSoftware, 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("DOCUMENT_ROOT", conf->DocumentRoot, 1);
        setenv("REMOTE_ADDR", req->remoteAddr, 1);
        setenv("REQUEST_URI", req->uri, 1);
        setenv("REQUEST_METHOD", get_str_method(req->reqMethod), 1);
        setenv("SERVER_PROTOCOL", get_str_http_prot(req->httpProt), 1);
        if(req->req_hd.iHost >= 0)
            setenv("HTTP_HOST", req->reqHeadersValue[req->req_hd.iHost], 1);
        if(req->req_hd.iReferer >= 0)
            setenv("HTTP_REFERER", req->reqHeadersValue[req->req_hd.iReferer], 1);
        if(req->req_hd.iUserAgent >= 0)
            setenv("HTTP_USER_AGENT", req->reqHeadersValue[req->req_hd.iUserAgent], 1);
        setenv("SCRIPT_NAME", str_ptr(&req->scriptName), 1);
        setenv("SCRIPT_FILENAME", str_ptr(&req->path), 1);
        if(req->reqMethod == M_POST)
        {
            if(req->req_hd.iReqContentType >= 0)
                setenv("CONTENT_TYPE", req->reqHeadersValue[req->req_hd.iReqContentType], 1);
            if(req->req_hd.iContentLength >= 0)
                setenv("CONTENT_LENGTH", req->reqHeadersValue[req->req_hd.iContentLength], 1);
        }

        setenv("QUERY_STRING", req->sReqParam ? req->sReqParam : "", 1);

        if(req->scriptType == cgi_ex)
        {
            execl(str_ptr(&req->path), base_name(str_ptr(&req->scriptName)), NULL);
        }
        else if(req->scriptType == php_cgi)
        {
            execl(conf->PathPHP, base_name(conf->PathPHP), NULL);
        }

    err_child:
        get_time(s, sizeof(s));
        printf( "Status: 500 Internal Server Error\r\n"
                "Content-type: text/html; charset=utf-8\r\n"
                "\r\n"
                "<!DOCTYPE html>\n"
                "<html>\n"
                " <head>\n"
                "  <title>500 Internal Server Error</title>\n"
                "  <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
                " </head>\n"
                " <body>\n"
                "  <h3> 500 Internal Server Error</h3>\n"
                "  <p>%s(%d)</p>\n"
                "  <hr>\n"
                "  %s\n"
                " </body>\n"
                "</html>", strerror(errno), errno, s);

        exit(EXIT_FAILURE);
    }
    else
    {
    //=========================== parent ===============================
        close(serv_cgi[0]);
        close(cgi_serv[1]);
        //------------ write to script ------------
        if(req->reqMethod == M_POST)
        {
            if (req->tail)
            {
                wr_bytes = write_timeout(serv_cgi[1], req->tail, req->lenTail, conf->TimeoutCGI);
                if (wr_bytes < 0)
                {
                    print__err(req, "<%s:%d> Error tail to script: %d\n", __func__, __LINE__, wr_bytes);
                    close(cgi_serv[0]);
                    close(serv_cgi[1]);
                    return kill_script(req, pid, RS500, "2");
                }
                req->req_hd.reqContentLength -= wr_bytes;
            }
            
            wr_bytes = client_to_script(req->clientSocket, serv_cgi[1], &req->req_hd.reqContentLength, req->numReq);
            if(wr_bytes < 0)
            {
                if (req->req_hd.reqContentLength > 0 && req->req_hd.reqContentLength < conf->ClientMaxBodySize)
                    client_to_cosmos(req->clientSocket, (long)req->req_hd.reqContentLength);

                print__err(req, "<%s:%d> Error client_to_script() = %d\n", __func__, __LINE__, wr_bytes);
                close(cgi_serv[0]);
                close(serv_cgi[1]);
                return kill_script(req, pid, RS500, "2");
            }
        }

        close(serv_cgi[1]);
        int ret;
        String hdrs = str_init(256);
        if (hdrs.err == 0)
        {
            ret = cgi_read_headers(req, &hdrs, cgi_serv[0]);
            str_free(&hdrs);
        }
        else
        {
            print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
            ret = -1;
        }
        
        close(cgi_serv[0]);
        if (ret < 0)
            return kill_script(req, pid, 0, "");
        else
            return wait_pid(req, pid);
        return ret;
    }

    return 0;
}
//======================================================================
int cgi(Connect *req)
{
    int ret;

    if (req->reqMethod == M_POST)
    {
        if (req->req_hd.iReqContentType < 0)
        {
            print__err(req, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if (req->req_hd.reqContentLength < 0)
        {
            print__err(req, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
        }

        if (req->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print__err(req, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, req->req_hd.reqContentLength);
            return -RS413;
        }
    }

    if (timedwait_close_cgi())
    {
        return -1;
    }
    //------------------------------------------------------------------
    str_cpy(&req->scriptName, req->decodeUri);
    if (req->scriptName.err)
    {
        print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
        return -RS500;
    }

    ret = cgi_fork(req);
    cgi_dec();
    if (ret < 0)
        req->connKeepAlive = 0;
    return ret;
}
