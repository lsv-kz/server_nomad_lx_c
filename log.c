#include "server.h"

int flog = STDOUT_FILENO, flog_err = STDERR_FILENO;
//pthread_mutex_t mtx_log = PTHREAD_MUTEX_INITIALIZER;
//======================================================================
void create_logfiles(const char *log_dir, const char * ServerSoftware)
{
    char buf[256];
    char s[4096];
    struct tm tm1;
    time_t t1;

    time(&t1);
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%Hh%Mm%Ss", &tm1);
    snprintf(s, sizeof(s), "%s/%s-%s.log", log_dir, buf, ServerSoftware);

    flog = open(s, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(flog == -1)
    {
        fprintf(stderr,"Error create logfile: %s; cwd: %s\n", s, getcwd(buf, sizeof(buf)));
        exit(1);
    }

    snprintf(s, sizeof(s), "%s/%s-%s-error.log", log_dir, buf, ServerSoftware);
    flog_err = open(s, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(flog_err == -1)
    {
        fprintf(stderr,"Error create log_err: %s\n", s);
        exit(1);
    }

    dup2(flog_err, STDERR_FILENO);
}
//======================================================================
void close_logs(void)
{
    close(flog);
    close(flog_err);
}
//======================================================================
void print_err(const char *format, ...)
{
    va_list ap;
    char buf[256];

    buf[0] = '[';
    get_time(buf + 1, sizeof(buf) - 1);
    int len = strlen(buf);
    memcpy(buf + len, "] - ", 5);
    len = strlen(buf);
    va_start(ap, format);
    vsnprintf(buf + len, sizeof(buf) - len, format, ap);
    va_end(ap);

    if (flog_err > 0)
        write(flog_err, buf, strlen(buf));
    else
        fwrite(buf, 1, strlen(buf), stderr);
}
//======================================================================
void print__err(Connect *req, const char *format, ...)
{
    va_list ap;
    char buf[256];
    buf[0] = '[';
    get_time(buf + 1, sizeof(buf) - 1);
    int len = strlen(buf);
    memcpy(buf + len, "] - ", 5);
    len = strlen(buf);
    snprintf(buf + len, sizeof(buf) - len, "[%u/%u/%u] ", req->numProc, req->numConn, req->numReq);
    len = strlen(buf);
    va_start(ap, format);
    vsnprintf(buf + len, sizeof(buf) - len, format, ap);
    va_end(ap);

    if (flog_err > 0)
        write(flog_err, buf, strlen(buf));
    else
        fwrite(buf, 1, strlen(buf), stderr);
}
//======================================================================
void print_log(Connect *req)
{
    int n, size = strlen(req->decodeUri) + ((req->req_hd.iReferer >= 0) ? strlen(req->reqHeadersValue[req->req_hd.iReferer]) : 0) +
               ((req->req_hd.iUserAgent >= 0) ? strlen(req->reqHeadersValue[req->req_hd.iUserAgent]) : 0) + 
               ((req->sReqParam) ? strlen(req->sReqParam) : 0) + 150;
    char *buf = malloc(size);
    if (!buf)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return;
    }
    
    if (req->reqMethod <= 0)
    {
        n = snprintf(buf, size, "%d/%d/%d - %s - [%s] - \"-\" %d %lld \"-\" \"-\"\n",
                req->numProc,
                req->numConn,
                req->numReq,
                req->remoteAddr,
                req->sLogTime, 
                req->respStatus,
                req->send_bytes);
    }
    else
    {
        n = snprintf(buf, size, "%d/%d/%d - %s - [%s] - \"%s %s%s%s %s\" %d %lld \"%s\" \"%s\"\n",
                req->numProc,
                req->numConn,
                req->numReq,
                req->remoteAddr,
                req->sLogTime, 
                get_str_method(req->reqMethod),
                req->decodeUri,
                (req->sReqParam) ? "?" : "",
                (req->sReqParam) ? req->sReqParam : "",
                get_str_http_prot(req->httpProt),
                req->respStatus,
                req->send_bytes,
                (req->req_hd.iReferer >= 0) ? req->reqHeadersValue[req->req_hd.iReferer] : "-",
                (req->req_hd.iUserAgent >= 0) ? req->reqHeadersValue[req->req_hd.iUserAgent] : "-");
    }

    if (n >= size)
        buf[size - 2] = '\n';
//pthread_mutex_lock(&mtx_log);
    write(flog, buf, strlen(buf));
//pthread_mutex_unlock(&mtx_log);
    //fprintf(stderr, "%d/%d/%d <%s:%d> size=%d, len=%d\n", req->numProc, req->numConn, req->numReq, __func__, __LINE__, size, n);
    free(buf);
}
