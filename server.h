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

#define    LINUX_
//#define    FREEBSD_
#define    SEND_FILE_
#define    TCP_CORK_

#define    MAX_PATH           4096
#define    MAX_NAME            256
#define    SIZE_BUF_REQUEST   8192
#define    MAX_HEADERS          25
#define    PROC_LIMIT            8
#define    ERR_TRY_AGAIN     -1000
#define    CGI_BUF_SIZE       4096
#define    FCGI_BUF_SIZE      2048

extern const char *boundary;

enum CGI_TYPE { NO_CGI, CGI, PHPCGI, PHPFPM, FASTCGI, SCGI, };

typedef struct fcgi_list_addr {
    char *script_name;
    char *addr;
    enum CGI_TYPE type;
    struct fcgi_list_addr *next;
} fcgi_list_addr;

typedef struct
{
    char *name;
    char *val;
} Param;

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

enum { false, true };

enum MODE_SEND { NO_CHUNK, CHUNK, CHUNK_END };
enum SOURCE_ENTITY { NO_ENTITY, FROM_FILE, FROM_DATA_BUFFER, MULTIPART_ENTITY, };
enum OPERATION_TYPE { READ_REQUEST = 1, SEND_RESP_HEADERS, SEND_ENTITY, DYN_PAGE, };
enum MULTIPART { SEND_HEADERS = 1, SEND_PART, SEND_END };
enum IO_STATUS { POLL = 1, WORK };

enum DIRECT { FROM_CGI = 1, TO_CGI, FROM_CLIENT, TO_CLIENT };

enum FCGI_OPERATION { FASTCGI_CONNECT = 1, FASTCGI_BEGIN, FASTCGI_PARAMS, FASTCGI_STDIN, 
                      FASTCGI_READ_HTTP_HEADERS, FASTCGI_SEND_HTTP_HEADERS, 
                      FASTCGI_SEND_ENTITY, FASTCGI_READ_ERROR, FASTCGI_CLOSE };


enum FCGI_STATUS {FCGI_READ_DATA = 1,  FCGI_READ_HEADER, FCGI_READ_PADDING }; 

enum CGI_OPERATION { CGI_CREATE_PROC = 1, CGI_STDIN, CGI_READ_HTTP_HEADERS, CGI_SEND_HTTP_HEADERS, CGI_SEND_ENTITY };

enum SCGI_OPERATION { SCGI_CONNECT = 1, SCGI_PARAMS, SCGI_STDIN, SCGI_READ_HTTP_HEADERS, SCGI_SEND_HTTP_HEADERS, SCGI_SEND_ENTITY };

//----------------------------------------------------------------------
typedef struct {
    unsigned int len;
    unsigned int ind;
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
//----------------------------------------------------------------------
union OPERATION { enum CGI_OPERATION cgi; enum FCGI_OPERATION fcgi; enum SCGI_OPERATION scgi;};
//======================================================================
typedef struct Config
{
    char ServerSoftware[48];
    char ServerAddr[128];
    char ServerPort[16];

    char DocumentRoot[MAX_PATH];
    char ScriptPath[MAX_PATH];
    char LogPath[MAX_PATH];
    char PidFilePath[512];

    char UsePHP[16];
    char PathPHP[MAX_PATH];

    int ListenBacklog;
    char TcpCork;
    char TcpNoDelay;

    char SendFile;
    int SndBufSize;

    int MaxWorkConnections;

    char BalancedLoad;

    unsigned int NumProc;
    unsigned int NumThreads;
    unsigned int MaxCgiProc;

    int MaxRequestsPerClient;
    int TimeoutKeepAlive;
    int Timeout;
    int TimeoutCGI;
    int TimeoutPoll;

    int MaxRanges;

    long int ClientMaxBodySize;

    char ShowMediaFiles;

    char index_html;
    char index_php;
    char index_pl;
    char index_fcgi;

    char user[32];
    char group[32];
    uid_t server_uid;
    gid_t server_gid;

    fcgi_list_addr *fcgi_list;
} Config;

extern const struct Config* const conf;
//======================================================================
typedef struct
{
    int  iConnection;
    int  iHost;
    int  iUserAgent;
    int  iReferer;
    int  iUpgrade;
    int  iReqContentType;
    int  iContentLength;
    int  iAcceptEncoding;
    int  iRange;
    int  iIf_Range;
    long long reqContentLength;
} ReqHd;
//----------------------------------------------------------------------
typedef struct Connect{
    struct Connect *prev;
    struct Connect *next;

    unsigned int numProc, numReq, numConn;
    int  serverSocket;
    int  serverPort;
    int  clientSocket;

    time_t sock_timer;
    int  timeout;
    int  event;
    enum OPERATION_TYPE operation;
    enum IO_STATUS    io_status;

    int  err;
    char remoteAddr[64];

    char bufReq[SIZE_BUF_REQUEST];

    int  lenBufReq;
    char *p_newline;
    char *tail;
    int  lenTail;

    int  reqMethod;

    char *uri;
    unsigned int uriLen;

    char decodeUri[SIZE_BUF_REQUEST];
    unsigned int lenDecodeUri;
    //------------------------------------------------------------------
    String resp_headers;
    String hdrs;
    String html;
    String msg;
    String scriptName;
    String path;

    char *sReqParam;
    char *sRange;
    int  httpProt;

    int  connKeepAlive;

    ReqHd req_hd;

    int  countReqHeaders;
    char *reqHeadersName[MAX_HEADERS + 1];
    const char *reqHeadersValue[MAX_HEADERS + 1];

    enum CGI_TYPE cgi_type;
    char cgi_buf[8 + CGI_BUF_SIZE + 8];
    struct
    {
        union OPERATION op;
        enum DIRECT dir;
        long len_buf;
        long len_post;
        char *p;
    
        pid_t pid;
        int  to_script;
        int  from_script;
    } cgi;
    
    struct
    {
        int http_headers_received;
        enum FCGI_STATUS status;
        int fd;

        int i_param;
        int size_par;
        Param vPar[32];

        unsigned char fcgi_type;
        int dataLen;
        int paddingLen;
        char *ptr_wr;
        char *ptr_rd;
        char buf[FCGI_BUF_SIZE + 8];
        int len_buf;
    } fcgi;

    int  respStatus;
    char sLogTime[64];

    long long fileSize;
    long long respContentLength;
    const char *respContentType;

    int  countRespHeaders;

    enum SOURCE_ENTITY source_entity;
    enum MODE_SEND mode_send;

    Range *rangeBytes;
    enum MULTIPART mp_status;
    int  numPart;
    int  indPart;

    int  fd;
    off_t offset;
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
int create_multipart_head(Connect *r);
int response(Connect *req);
int options(Connect *req);
//----------------------------------------------------------------------
int create_fcgi_socket(Connect *r, const char *host);
int read_request_headers(Connect *r);
int write_to_client(Connect *r, const char *buf, int len);
int read_from_client(Connect *r, char *buf, int len);
int send_fd(int unix_sock, int fd, void *data, int size_data);
int recv_fd(int unix_sock, int num_chld, void *data, int *size_data);
//----------------------------------------------------------------------
int encode(const char *s_in, char *s_out, int len_out);
int decode(const char *s_in, int len_in, char *s_out, int len_out);
//----------------------------------------------------------------------
int create_response_headers(Connect *req);
int create_message(Connect *req, const char *msg);
const char *status_resp(int st);
//----------------------------------------------------------------------
int get_time(char *s, int size_buf);
int log_time(char *s, int size_buf);
const char *strstr_case(const char * s1, const char *s2);
int strlcmp_case(const char *s1, const char *s2, int len);

int get_int_method(char *s);
const char *get_str_method(int i);

int get_int_http_prot(char *s);
const char *get_str_http_prot(int i);

const char *get_str_operation(enum OPERATION_TYPE n);
const char *get_cgi_operation(enum CGI_OPERATION n);
const char *get_fcgi_operation(enum FCGI_OPERATION n);
const char *get_fcgi_status(enum FCGI_STATUS n);
const char *get_scgi_operation(enum SCGI_OPERATION n);
const char *get_cgi_type(enum CGI_TYPE n);
const char *get_cgi_dir(enum DIRECT n);

int str_num(const char *s);
int clean_path(char *path);
char *content_type(const char *s);
const char *base_name(const char *path);
int parse_startline_request(Connect *req, char *s);
int parse_headers(Connect *req, char *s, int i);
int find_empty_line(Connect *req);
void init_struct_request(Connect *req);
void init_strings_request(Connect *r);
void free_strings_request(Connect *r);
//----------------------------------------------------------------------
void close_logs(void);
void print(const char *format, ...);
void print_err(const char *format, ...);
void print__err(Connect *req, const char *format, ...);
void print_log(Connect *req);
//----------------------------------------------------------------------
void close_req(void);
void push_resp_list(Connect *req);
Connect *pop_resp_list(void);
void end_response(Connect *req);
void free_range(Connect *r);
//----------------------------------------------------------------------
void StrInit(String *s);
void StrFree(String *s);
void StrClear(String *s);
void StrReserve(String *s, unsigned int n);
void StrResize(String *s, unsigned int n);
int StrLen(String *s);
int StrSize(String *s);
const char *StrPtr(String *s);
void StrCpy(String *s, const char *cs);
void StrCpyLN(String *s, const char *cs);
void StrCat(String *s, const char *cs);
void StrCatLN(String *s, const char *cs);
void StrnCat(String *s, const char *cs, unsigned int len);
void StrCatInt(String *s, long long ll);
void StrCatIntLN(String *s, long long ll);
//----------------------------------------------------------------------
void free_fcgi_list();
void set_max_conn(int n);
long get_lim_max_fd(long *max, long *cur);
int set_max_fd(int min_open_fd);
//----------------------------------------------------------------------
void *event_handler(void *arg);
void push_send_file(Connect *req);
void push_send_multipart(Connect *r);
void push_send_html(Connect *r);
void push_pollin_list(Connect *req);
void close_event_handler();
//----------------------------------------------------------------------
void push_cgi(Connect *r);
void *cgi_handler(void *arg);
void close_cgi_handler();
void free_fcgi_param(Connect *r);
//----------------------------------------------------------------------
void close_manager();

#endif
