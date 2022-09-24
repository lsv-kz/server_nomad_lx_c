#include "server.h"

const char *status_resp(int st);
//======================================================================
void response_status(String *s, const char *prot, const char *stat)
{
    if (s->err) return;
    str_cat(s, prot);
    str_cat(s, " ");
    str_cat_ln(s, stat);
}
//======================================================================
void create_response_headers(Connect *req, String *hd, String *hdrs)
{
    char time_resp[64];
    get_time(time_resp, sizeof(time_resp));

    response_status(hd, get_str_http_prot(req->httpProt), status_resp(req->respStatus));

    str_cat(hd, "Date: ");
    str_cat_ln(hd, time_resp);

    str_cat(hd, "Server: ");
    str_cat_ln(hd, conf->ServerSoftware);

    if(req->reqMethod == M_OPTIONS)
        str_cat(hd, "Allow: OPTIONS, GET, HEAD, POST\r\n");

    if (req->numPart == 1)
    {
        str_cat(hd, "Content-Type: ");
        str_cat_ln(hd, req->respContentType);
        
        str_cat(hd, "Content-Length: ");
        str_llint_ln(hd, req->respContentLength);
        
        str_cat(hd, "Content-Range: bytes ");
        str_llint(hd, req->offset);
        str_cat(hd, "-");
        str_llint(hd, req->offset + req->respContentLength - 1);
        str_cat(hd, "/");
        str_llint_ln(hd, req->fileSize);
    }
    else if (req->numPart == 0)
    {
        if (req->respContentType)
        {
            str_cat(hd, "Content-Type: ");
            str_cat_ln(hd, req->respContentType);
        }
        if (req->respContentLength >= 0)
        {
            str_cat(hd, "Content-Length: ");
            str_llint_ln(hd, req->respContentLength);
            
            if (req->respStatus == RS200)
                str_cat(hd, "Accept-Ranges: bytes\r\n");
        }

        if (req->respStatus == RS416)
        {
            str_cat(hd, "Content-Range: bytes */");
            str_llint_ln(hd, req->fileSize);
        }
    }

    if (req->respStatus == RS101)
    {
        str_cat(hd, "Upgrade: HTTP/1.1\r\n"
                    "Connection: Upgrade\r\n");
    }
    else
    {
        if (req->connKeepAlive == 0)
            str_cat(hd, "Connection: close\r\n");
        else
            str_cat(hd, "Connection: keep-alive\r\n");
    }
    //------------------------------------------------------------------
    if (hdrs)
    {
        str_cat(hd, str_ptr(hdrs));
    }

    str_cat(hd,  "\r\n");

    if (hd->err)
    {
        print_err("<%s:%d> Error create response headers: %d\n", __func__, __LINE__, hd->err);
        return;
    }

    int n = write_timeout(req->clientSocket, str_ptr(hd), str_len(hd), conf->Timeout);
    if(n <= 0)
    {
        print_err("<%s:%d> Sent to client response error; (%d)\n", __func__, __LINE__, n);
        hd->err = -1;
        return;
    }
}
//======================================================================
int send_response_headers(Connect *req, String *hdrs)
{
    String hd = str_init(768);
    if (hd.err == 0)
    {
        create_response_headers(req, &hd, hdrs);
        str_free(&hd);
    }
    else
    {
        print_err("<%s:%d> Error: malloc()\n", __func__, __LINE__);
        return -1;
    }
    
    return 0;
}
//======================================================================
void create_html(Connect *req, String *s, const char *msg)
{
    const char *title = status_resp(req->respStatus);
    str_cat(s, "<!DOCTYPE html>\n"
                "<html>\n"
                " <head>\n"
                "  <title>");
    str_cat(s, title);
    str_cat(s, "</title>\n"
                "  <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
                "  <link href=\"/styles.css\" type=\"text/css\" rel=\"stylesheet\">"
                " </head>\n"
                " <body>\n"
                "  <h3>");
    str_cat(s, title);
    str_cat(s, "</h3>\n"
                "  <p>");
    str_cat(s, msg);
    str_cat(s, "</p>\n"
                "  <hr>\n"
                "  ");
    str_cat(s, req->sLogTime);
    str_cat(s, "\n"
                " </body>\n"
                "</html>");
}
//======================================================================
void send_message(Connect *req, String *hdrs, const char *msg)
{
    req->respContentType = "text/html";

    if (req->httpProt == 0)
        req->httpProt = HTTP11;

    if(req->respStatus == RS204)
    {
        req->respContentLength = 0;
        send_response_headers(req, NULL);
        return;
    }
    else if (req->reqMethod == M_HEAD)
    {
        send_response_headers(req, NULL);
        return;
    }
    else
    {
        String html = str_init(512);
        create_html(req, &html, msg);
        if (html.err == 0)
        {
            req->respContentLength = html.len;
            if(send_response_headers(req, hdrs))
            {
                print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
                req->connKeepAlive = 0;
            }
            else
            {
                req->send_bytes = write_timeout(req->clientSocket, str_ptr(&html), req->respContentLength, conf->Timeout);
                if(req->send_bytes <= 0)
                {
                    print_err("<%s:%d> Error write_timeout()\n", __func__, __LINE__);
                    req->connKeepAlive = 0;
                }
            }
        }
        else
        {
            print_err("<%s:%d> Error create HTML file\n", __func__, __LINE__);
            req->connKeepAlive = 0;
            return;
        }
        str_free(&html);
    }
}
//======================================================================
const char *status_resp(int st)
{
    switch(st)
    {
        case RS101:
            return "101 Switching Protocols";
        case RS200:
            return "200 OK";
        case RS204:
            return "204 No Content";
        case RS206:
            return "206 Partial Content";
        case RS301:
            return "301 Moved Permanently";
        case RS302:
            return "302 Moved Temporarily";
        case RS400:
            return "400 Bad Request";
        case RS401:
            return "401 Unauthorized";
        case RS402:
            return "402 Payment Required";
        case RS403:
            return "403 Forbidden";
        case RS404:
            return "404 Not Found";
        case RS405:
            return "405 Method Not Allowed";
        case RS406:
            return "406 Not Acceptable";
        case RS407:
            return "407 Proxy Authentication Required";
        case RS408:
            return "408 Request Timeout";
        case RS411:
            return "411 Length Required";
        case RS413:
            return "413 Request entity too large";
        case RS414:
            return "414 Request-URI Too Large";
        case RS416:
            return "416 Range Not Satisfiable";
        case RS418:
            return "418 I'm a teapot";
        case RS500:
            return "500 Internal Server Error";
        case RS501:
            return "501 Not Implemented";
        case RS502:
            return "502 Bad Gateway";
        case RS503:
            return "503 Service Unavailable";
        case RS504:
            return "504 Gateway Time-out";
        case RS505:
            return "505 HTTP Version not supported";
        default:
            return "500 Internal Server Error";
    }
    return "";
}
