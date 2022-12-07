#include "server.h"

int flog = STDOUT_FILENO, flog_err = STDERR_FILENO;
//pthread_mutex_t mtx_log = PTHREAD_MUTEX_INITIALIZER;
//======================================================================
void create_logfiles(const char *log_dir)
{
    char buf[256];
    char s[4096];
    struct tm tm1;
    time_t t1;

    time(&t1);
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M", &tm1);
    snprintf(s, sizeof(s), "%s/%s_%s.log", log_dir, buf, conf->ServerSoftware);

    flog = open(s, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog == -1)
    {
        fprintf(stderr,"Error create logfile: %s; cwd: %s\n", s, getcwd(buf, sizeof(buf)));
        exit(1);
    }

    snprintf(s, sizeof(s), "%s/error_%s_%s.log", log_dir, buf, conf->ServerSoftware);
    flog_err = open(s, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog_err == -1)
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
    log_time(buf + 1, sizeof(buf) - 1);
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
    char buf[300];
    buf[0] = '[';
    log_time(buf + 1, sizeof(buf) - 1);
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
int print_log_(Connect *req, int size)
{
    int n;
    char buf[size];
    char log_tm[32];
    log_time(log_tm, sizeof(log_tm));

    if (req->reqMethod <= 0)
    {
        n = snprintf(buf, size, "%d/%d/%d - %s - [%s] - \"-\" %d %lld \"-\" \"-\"\n",
                req->numProc,
                req->numConn,
                req->numReq,
                req->remoteAddr,
                log_tm,
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
                log_tm,
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
        return n + 1;
//pthread_mutex_lock(&mtx_log);
    write(flog, buf, strlen(buf));
//pthread_mutex_unlock(&mtx_log);
    return 0;
}
//----------------------------------------------------------------------
void print_log(Connect *req)
{
    int size = 300;
    int n = print_log_(req, size);
    if (n)
    {
        if (n > 1024)
            fprintf(stderr, "<%s:%d> Error (print_log()=%d) > 1024\n", __func__, __LINE__, n);
        else
            n = print_log_(req, n);
    }
}
