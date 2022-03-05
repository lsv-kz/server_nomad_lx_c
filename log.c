#include "server.h"

int flog, flog_err;
pthread_mutex_t mtx_log = PTHREAD_MUTEX_INITIALIZER;
//======================================================================
void create_logfiles(const char *log_dir, const char * ServerSoftware)
{
    char buf[256];
    char s[4096];
    struct tm tm1;
    time_t t1;

    time(&t1);
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-m%m-%d_%Hh%Mm%Ss", &tm1);
    snprintf(s, sizeof(s), "%s/%s-%s.log", log_dir, buf, ServerSoftware);

    flog = open(s, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(flog == -1)
    {
        perror("  Error create logfile");
        fprintf(stderr,"  Error create logfile: %s; cwd: %s\n", s, getcwd(buf, sizeof(buf)));
        exit(1);
    }
    
    struct flock flck;
    flck.l_type = F_WRLCK;
    flck.l_whence = SEEK_SET;
    flck.l_start = 0;
    flck.l_len = 0;
    fcntl(flog, F_SETLK, &flck);

    snprintf(s, sizeof(s), "%s/%s-%s", log_dir, ServerSoftware, "error.log");
    flog_err = open(s, O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(flog_err == -1)
    {
        fprintf(stderr,"  Error create log_err: %s\n", s);
        exit(1);
    }
    
    flck.l_type = F_WRLCK;
    flck.l_whence = SEEK_SET;
    flck.l_start = 0;
    flck.l_len = 0;
    fcntl(flog_err, F_SETLK, &flck);
    
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
    strcat(buf, "] - ");
    int len = strlen(buf);
    va_start(ap, format);
    vsnprintf(buf + len, sizeof(buf) - len, format, ap);
    va_end(ap);
pthread_mutex_lock(&mtx_log);   
    write(flog_err, buf, strlen(buf));
pthread_mutex_unlock(&mtx_log);
}
//======================================================================
void print__err(Connect *req, const char *format, ...)
{
    va_list ap;
    char buf[256];
    buf[0] = '[';
    get_time(buf + 1, sizeof(buf) - 1);
    strcat(buf, "] - ");
    int len = strlen(buf);
    snprintf(buf + len, sizeof(buf) - len, "[%u/%u/%u] ", req->numChld, req->numConn, req->numReq);
    len = strlen(buf);
    va_start(ap, format);
    vsnprintf(buf + len, sizeof(buf) - len, format, ap);
    va_end(ap);
pthread_mutex_lock(&mtx_log);
    write(flog_err, buf, strlen(buf));
pthread_mutex_unlock(&mtx_log);
}
//======================================================================
void print_log(Connect *req)
{
    const int size = 320;
    char buf[size];
    int len;
        
    snprintf(buf, size,"%d/%d/%d - %s - [%s] - ",     
                req->numChld,
                req->numConn,
                req->numReq,
                req->remoteAddr,
                req->sLogTime);
    len = strlen(buf);
    
    if (req->reqMethod > 0)
        snprintf(buf + len, size - len, "\"%s %s %s\" ", 
                get_str_method(req->reqMethod),
                req->decodeUri,
                get_str_http_prot(req->httpProt));
    else
        snprintf(buf + len, size - len, "\"-\" ");
        
    len = strlen(buf);
    int n = snprintf(buf + len, size - len, "%d %lld \"%s\" \"%s\"\n",
                req->respStatus,
                req->send_bytes,
                (req->iReferer >= 0) ? req->reqHeadersValue[req->iReferer] : "-",
                (req->iUserAgent >= 0) ? req->reqHeadersValue[req->iUserAgent] : "-");
    if (n >= (size - len))
        buf[size - 2] = '\n';
pthread_mutex_lock(&mtx_log);
    write(flog, buf, strlen(buf));
pthread_mutex_unlock(&mtx_log);
}
