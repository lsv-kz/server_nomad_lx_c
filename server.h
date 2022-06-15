#ifndef SERVER_H_
#define SERVER_H_

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <pwd.h>
#include <grp.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>

#define     MAX_PATH           4096
#define     MAX_NAME            256
#define     LEN_BUF_REQUEST    8192
#define     MAX_HEADERS          25
#define     NO_PRINT_LOG      -1000

typedef struct fcgi_list_addr {
    char *scrpt_name;
    char *addr;
    struct fcgi_list_addr *next;
} fcgi_list_addr;

enum {
    RS101 = 101,
    RS200 = 200,RS204 = 204,RS206 = 206,
    RS301 = 301, RS302,
    RS400 = 400,RS401,RS402,RS403,RS404,RS405,RS406,RS407,
    RS408,RS411 = 411,RS413 = 413,RS414,RS415,RS416,RS417,RS418,
    RS500 = 500,RS501,RS502,RS503,RS504,RS505
};

enum {
    M_GET = 1, M_HEAD, M_POST, M_OPTIONS, M_PUT,
    M_PATCH, M_DELETE, M_TRACE, M_CONNECT   
};

enum { HTTP09 = 1, HTTP10, HTTP11, HTTP2 };

enum { cgi_ex = 1, php_cgi, cgi_ex2, php_fpm, fast_cgi};

enum { EXIT_THR = 1};
//----------------------------------------------------------------------
typedef struct {
    unsigned int len;
    unsigned int size;
    int err;
    char *ptr;
} String;
//----------------------------------------------------------------------
typedef struct {
    long long start;
    long long end;
    long long len;
} Range;
//======================================================================
struct Config
{
    char host[128];
    char servPort[16];
    char ServerSoftware[48];

    char tcp_cork;

    char TcpNoDelay;

    int NumProc;
    int MaxThreads;
    int MinThreads;
    int MaxRequestsPerThr;

    int SNDBUF_SIZE;

    char SEND_FILE;

    int MAX_SND_FD;
    int TIMEOUT_POLL;

    int MaxProcCgi;

    int ListenBacklog;

    int MAX_REQUESTS;

    char KeepAlive;
    int TimeoutKeepAlive;
    int TimeOut;
    int TimeoutCGI;

    int MaxRanges;

    char rootDir[MAX_PATH];
    char cgiDir[MAX_PATH];
    char logDir[MAX_PATH];

    long int ClientMaxBodySize;

    char UsePHP[16];
    char PathPHP[MAX_PATH];

    fcgi_list_addr *fcgi_list;

    char index_html;
    char index_php;
    char index_pl;
    char index_fcgi;

    char ShowMediaFiles;

    uid_t server_uid;
    gid_t server_gid;

    char user[32];
    char group[32];
};

extern const struct Config* const conf;
//======================================================================
typedef struct hdr {
    char *ptr;
    int len;
} hdr;

typedef struct Connect{
    struct Connect *prev;
    struct Connect *next;
    
    unsigned int numProc, numReq, numConn;
    int       serverSocket;
    int       serverPort;
    int       clientSocket;
    
    time_t    sock_timer;
    int       timeout;
    int       event;
    int       first_snd;
    
    int       err;
    char      remoteAddr[64];
    
    char      bufReq[LEN_BUF_REQUEST]; 
    
    int       i_bufReq;
    char      *p_newline;
    char      *tail;
    int       lenTail;
    int       i_arrHdrs;
    hdr       arrHdrs[MAX_HEADERS + 1];
    
    int       reqMethod;

    char      *uri;
    unsigned int uriLen;
    
    char      decodeUri[LEN_BUF_REQUEST];
    unsigned int lenDecodeUri;
    //------------------------------------------------------------------
    char      *sReqParam;
    char      *sRange;
    int       httpProt;

    int       connKeepAlive;

    int       iConnection;
    int       iHost;
    int       iUserAgent;
    int       iReferer;
    int       iUpgrade;
    int       iReqContentType;
    int       iContentLength;
    int       iAcceptEncoding;
    int       iRange;
    int       iIf_Range;
    
    long long reqContentLength;
    int       countReqHeaders;
    char      *reqHeadersName[MAX_HEADERS + 1];
    char      *reqHeadersValue[MAX_HEADERS + 1];
    
    const char  *scriptName;
    String    *path;
    int       sizePath;
    
    int       scriptType;
    int       respStatus;
    char      sLogTime[64];
    
    long long fileSize;
    long long respContentLength;
    const char  *respContentType;
    
    int       countRespHeaders;
    
    Range     *rangeBytes;
    int       numPart;
    
    int       fd;
    off_t     offset;
    long long send_bytes;
} Connect;
//----------------------------------------------------------------------
#define   CHUNK_SIZE_BUF   4096
#define MAX_LEN_SIZE_CHUNK  6
enum mode_chunk {NO_SEND = 0, SEND_NO_CHUNK, SEND_CHUNK};
typedef struct {
    int i;
    int mode;
    int allSend;
    int sock;
    int err;
    char buf[CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK + 10];
} chunked;

extern char **environ;
//----------------------------------------------------------------------
int response(Connect *req);
int options(Connect *req);
int cgi(Connect *req);
int fcgi(Connect *req);
void init_struct_request(Connect *req);
//----------------------------------------------------------------------
int create_client_socket(const char *host);
int unixBind(const char *path, int type);
int unixConnect(const char *path);
//----------------------------------------------------------------------
int encode(const char *s_in, char *s_out, int len_out);
int decode(const char *s_in, int len_in, char *s_out, int len_out);
//----------------------------------------------------------------------
int read_timeout(int fd, char *buf, int len, int timeout);
int write_timeout(int sock, const char *buf, int len, int timeout);
int client_to_script(int fd_in, int fd_out, long long *cont_len, int n);
long client_to_cosmos(int fd_in, long size);
long cgi_to_cosmos(int fd_in, int timeout);
long fcgi_to_cosmos(int fd_in, int size, int timeout);
int fcgi_read_padding(int fd_in, long len, int timeout);
int fcgi_read_stderr(int fd_in, int cont_len, int timeout);
int send_file_ux(int fd_out, int fd_in, char *buf, int *size, off_t offset, long long *cont_len);
int read_headers(Connect *req, int timeout1, int timeout2);

int hd_read(Connect *req);
int empty_line(Connect *req);

int send_fd(int unix_sock, int fd, void *data, int size_data);
int recv_fd(int unix_sock, int num_chld, void *data, int size_data);
//----------------------------------------------------------------------
void get_time_run(int a, int b, struct timeval *time1, struct timeval *time2);
void send_message(Connect *req, String *hdrs, const char *msg);
int send_response_headers(Connect *req, String *hdrs);
const char *status_resp(int st);
//----------------------------------------------------------------------
int get_time(char *s, int size_buf);
const char *strstr_case(const char * s1, const char *s2);
int strlcmp_case(const char *s1, const char *s2, int len);

int get_int_method(char *s);
const char *get_str_method(int i);

int get_int_http_prot(char *s);
const char *get_str_http_prot(int i);

int str_num(const char *s);
int clean_path(char *path);
char *content_type(const char *s);
const char *base_name(const char *path);
int parse_startline_request(Connect *req, char *s, int len);
int parse_headers(Connect *req, char *s, int len);
const char *str_err(int i);
//----------------------------------------------------------------------
void close_logs(void);
void print(const char *format, ...);
void print_err(const char *format, ...);
void print__err(Connect *req, const char *format, ...);
void print_log(Connect *req);
//----------------------------------------------------------------------
int exit_thr(void);
void start_req(void);
void wait_close_req(int num_chld, int n);
void timedwait_close_conn(void);
void close_req(void);
void push_resp_list(Connect *req);
Connect *pop_resp_list(void);
void end_response(Connect *req);
void free_range(Connect *r);
int end_thr(int);
//----------------------------------------------------------------------
int timedwait_close_cgi(void);
void cgi_dec();
//----------------------------------------------------------------------
void chunk_add_longlong(chunked *chk, long long ll);
void va_chunk_add_str(chunked *chk, int numArg, ...);
void chunk_add_arr(chunked *chk, char *s, int len);
void chunk_end(chunked *chk);
int cgi_to_client(chunked *chk, int fdPipe);
int fcgi_to_client(chunked *chk, int fdPipe, int len);
//----------------------------------------------------------------------
String str_init(unsigned int n);
void str_free(String *s);
void str_resize(String *s, unsigned int n);
void str_cat(String *s, const char *cs);
void str_cat_ln(String *s, const char *cs);
void str_llint(String *s, long long ll);
void str_llint_ln(String *s, long long ll);
const char *str_ptr(String *s);
int str_len(String *s);
//----------------------------------------------------------------------
void free_fcgi_list();
//----------------------------------------------------------------------
void *event_handler(void *arg);
void push_pollout_list(Connect *req);
void push_pollin_list(Connect *req);
void close_event_handler(void);

#endif
