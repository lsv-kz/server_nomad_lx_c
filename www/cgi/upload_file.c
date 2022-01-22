#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MAX_FILE_SIZE 6000000

char s_time[128];

/*====================================================================*/
void head_html(void)
{
    printf( "<!DOCTYPE html>\n"
            "<html>\n"
            " <head>\n"
            "  <meta charset=\"utf-8\">\n"
            "  <title>File downloaded</title>\n"
            "  <style>\n"
            "    body {\n"
            "     margin-left:100px;\n"
            "     margin-right:50px;\n"
            "     background: rgb(60,40,40);\n"
            "     color: gold;"
            "    }\n"
            "  </style>\n"
            " </head>\n"
            " <body>\n");
}
/*============================= str_cat ==============================*/
int str_cat(char *dst, char *src, int size_dst)
{
    int len_src = strlen(src);
    int len_dst = strlen(dst);
    int i;
    
    if(!dst || !src)
        return -1;

    if((size_dst - len_dst) <= 0)
        return -1;

    if((size_dst - len_dst) < (len_src + 1))
        return -1;

    for(i = 0; i < len_src; i++)
    {
        dst[i + len_dst] = src[i];
    }

    dst[i + len_dst] = '\0';
    return i;
}
/*============================ str_n_cat =============================*/
int str_n_cat(char *dst, char *src, int size_dst, int len)
{
    int len_dst = strlen(dst);
    int i;
    
    if(!dst || !src)
        return -1;

    if((size_dst - len_dst) <=0 )
        return -1;

    if((size_dst - len_dst) < (len + 1))
        return -1;

    for(i = 0; i < len; i++)
    {
        dst[i + len_dst] = src[i];
    }

    dst[i + len_dst] = '\0';
    return i;
}
/*============================= find_str =============================*/
int find_in_str(char *s_in, char *s_out, int size_out, char *s1, char *s2)
{
    char *p1, *p2;
    int len = strlen(s1);

    if((p1 = strstr(s_in, s1)))
    {
        p1 = p1 + len;      
        if(s2[0] == 0)          
            p2 = p1 + strlen(p1);
        else if(strstr(s2, "\r\n"))
            p2 = strpbrk(p1, "\r\n");
        else
            p2 = strstr(p1, s2);
        if(p2)
        {   
            if((p2 - p1) < size_out)
            {
                if(size_out > p2 - p1)
                {   
                //  strncpy(s_out, p1, p2 - p1);
                    memcpy(s_out, p1, p2 - p1);
                    s_out[p2 - p1] = 0;
                    return p2 - p1;
                }
                return -1;
            }
        }
    }
    return -1;
}
/*============================ read_line =============================*/
int read_line(char *buf, int size_buf, int *content_len)
{
    int n;
    
    buf[0] = '\0';
    if(!fgets (buf, size_buf, stdin))
    {
        if(!strlen(buf))
            return 0;
    }
    
    n = strlen(buf);
    *content_len -= n;
    return n;
}
/*========================== read_request ============================*/
int read_head(char *buf, int buf_size, int *content_len)
{
    int i = 0, len;
    char s[1024];
    
    *buf = 0;
    while(1)
    {
        if(!fgets (s, sizeof(s), stdin))
        {
            i = -1;
            break;
        }

        len = strlen(s);
        i += len;
        *content_len -= len;
        str_cat(buf, s, buf_size);
        if(!strcspn(s, "\r\n"))
            break;
    }
    return i;
}
/*======================== read_multipart_file =======================*/
int write_to_file(FILE *f, int len, int *content_len)
{
    int n, m;
    long long wr_bytes;
    char buf[2048];

    for(n = 0, wr_bytes = 0; len > 0; )
    {
        if(len > sizeof(buf))
            //------------- read from stdin -------------
            n = fread(buf, 1, sizeof(buf), stdin);
        else
            //------------- read from stdin -------------
            n = fread(buf, 1, len, stdin);

        if(n <= 0)
            return n;
        len -= n;
        buf[n]=0;
        *content_len -= n;
        m = fwrite(buf, 1, n, f);
        if(m == -1)
            return -2;
        wr_bytes = wr_bytes + m;
        if((m - n) != 0)
            return -3;
    }
    return wr_bytes;
}
/*====================================================================*/
void send_response(char *msg, int len, int stat)
{
    printf("Content-Type: text/html\r\n");
    printf("\r\n");

    head_html();
    printf( "   <h3>%s</h3>\n"
            "   <p> %d bytes</p>\n"
            "   <hr>\n"
            "   <form action=\"upload_file\" enctype=\"multipart/form-data\" method=\"post\">\n"
            "    <p>\n"
            "      What files are you sending?<br>\n"
            "      <input type=\"file\" name=\"filename\"><br>\n"
            "      <input type=\"submit\" value=\"Upload\">\n"
            "    </p>\n"
            "   </form>\n"
            "   <hr>\n"
            "   %s\n"
            " </body>\n"
            "</html>", msg, len, s_time);
    fprintf(stderr, "<%s %s():%d> %s\n  %s\n  status=%d\n", __FILE__, __FUNCTION__, __LINE__, msg, s_time, stat);

    exit(stat);
}
/*====================================================================*/
char *base_name(const char *path)
{
    char *p;

    p = strrchr(path, '/');
    if(p)
        return p+1;
    else
        return NULL;;
}
/*====================================================================*/
int main(int argc, char *argv[])
{
    FILE *f;
    int n, lenBound;
    int wr_bytes, content_len;
    char buf[4096], head[4096];
    char filename[4096];
    char path[4096];
    char boundary[256];
    char dir_name[] = "..Downloads";
    char *cont_type;
    char *doc_root;
    char *cont_len;
    time_t now = 0;
    struct tm *t;

    now = time(NULL);

    t = gmtime(&now);
    strftime(s_time, 128, "%a, %d %b %Y %H:%M:%S GMT", t);

    if(!(doc_root = getenv("DOCUMENT_ROOT")))
    {
        exit(EXIT_FAILURE);
    }

    if(!(cont_type = getenv("CONTENT_TYPE")))
    {
        send_response("Client Error: 400 Bad Request", 0, 0);
    }

    memcpy(boundary, "--", 3);
    find_in_str(cont_type, boundary + 2, sizeof(boundary) - 2, "boundary=", "");
    lenBound = strlen(boundary) - 2;
    
    if((cont_len = getenv("CONTENT_LENGTH")))
    {
        sscanf(cont_len, "%d", &content_len);

        if(content_len > MAX_FILE_SIZE)
        {
            send_response("Client Error: 413 Request entity too large", 0, 0);
        }
        if(content_len == 0)
        {
            send_response("", 0, 0);
        }
    }
    else
    {
        send_response("Client Error: 411 Length Required", 0, 0);
    }
    // read boundary from stdin: "-----------------------------57219994717188271971049985052\r\n"
    if(read_line(buf, sizeof(buf), &content_len) <= 0)
    {
        send_response("Client Error: 400 Bad Request", 0, 0);
    }

    n = strlen(buf) - 2;
    buf[n] = 0;

    if(memcmp(boundary, buf, lenBound + 2))
    {
        send_response("Client Error: 400 Bad Request", 0, 0);
    }

    //------------- read from stdin -------------
    n = read_head(head, sizeof(head), &content_len);
    if(n < 0)
    {
        send_response("Client Error: 400 Bad Request", 0, 0);
    }
    else if(n == 0)
    {
        send_response("?", 0, 0);
    }

    find_in_str(head, filename, 256, "filename=\"", "\"");
    if(filename[0] == 0)
    {
        send_response("400 Bad Request", 0, 0);
    }
    else
    {
        snprintf(path, sizeof(path), "%s/%s/%s", doc_root, dir_name, filename);

        f = fopen(path, "wb");
        if (!f)
        {
            char s[512];
            snprintf(s, 512, "Error: open(%s)", path);
            send_response(s, 0, 0);
        }
        //------------- read from stdin -------------
        wr_bytes = write_to_file(f, content_len - 6 - strlen(boundary), &content_len);
        if(wr_bytes <= 0)
        {
            fclose(f);
            send_response("Error write to file", 0, 1);
        }

        n = fread(buf, 1, 6 + strlen(boundary), stdin);
        content_len -= n;
        fclose(f);
    }
/*..........................  ......................*/
    send_response(base_name(path), wr_bytes, 0);

    exit(EXIT_SUCCESS);
}
