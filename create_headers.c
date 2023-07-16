#include "server.h"

const char *status_resp(int st);
//======================================================================
int create_response_headers(Connect *r)
{
    StrReserve(&r->resp_headers, 512);
    StrClear(&r->resp_headers);
    if (r->resp_headers.err)
    {
        print__err(r, "<%s:%d> Error StrReserve()\n", __func__, __LINE__);
        return -1;
    }

    StrCpy(&r->resp_headers, get_str_http_prot(r->httpProt));
    StrCat(&r->resp_headers, " ");
    StrCatLN(&r->resp_headers, status_resp(r->respStatus));
    
    StrCat(&r->resp_headers, "Date: ");
    StrCatLN(&r->resp_headers, r->sLogTime);
    
    StrCat(&r->resp_headers, "Server: ");
    StrCatLN(&r->resp_headers, conf->ServerSoftware);

    if (r->reqMethod == M_OPTIONS)
        StrCat(&r->resp_headers, "Allow: OPTIONS, GET, HEAD, POST\r\n");

    if (r->numPart == 1)
    {
        StrCat(&r->resp_headers, "Content-Type: ");
        StrCatLN(&r->resp_headers, r->respContentType);

        StrCat(&r->resp_headers, "Content-Length: ");
        StrCatIntLN(&r->resp_headers, r->respContentLength);

        StrCat(&r->resp_headers, "Content-Range: bytes ");
        StrCatInt(&r->resp_headers, r->offset);
        StrCat(&r->resp_headers, "-");
        StrCatInt(&r->resp_headers, r->offset + r->respContentLength - 1);
        StrCat(&r->resp_headers, "/");
        StrCatIntLN(&r->resp_headers, r->fileSize);
    }
    else if (r->numPart == 0)
    {
        if (r->respContentType)
        {
            StrCat(&r->resp_headers, "Content-Type: ");
            StrCatLN(&r->resp_headers, r->respContentType);
        }

        if (r->respContentLength >= 0)
        {
            StrCat(&r->resp_headers, "Content-Length: ");
            StrCatIntLN(&r->resp_headers, r->respContentLength);

            if (r->respStatus == RS200)
            {
                StrCatLN(&r->resp_headers, "Accept-Ranges: bytes");
            }
        }

        if (r->respStatus == RS416)
        {
            StrCat(&r->resp_headers, "Content-Range: bytes */");
            StrCatIntLN(&r->resp_headers, r->fileSize);
        }
    }

    if (r->respStatus == RS101)
    {
        StrCat(&r->resp_headers, "Upgrade: HTTP/1.1\r\nConnection: Upgrade\r\n");
    }
    else
    {
        if (r->connKeepAlive == 0)
            StrCat(&r->resp_headers, "Connection: close\r\n");
        else
            StrCat(&r->resp_headers, "Connection: keep-alive\r\n");
    }
    
    if (r->mode_send == CHUNK)
        StrCat(&r->resp_headers, "Transfer-Encoding: chunked\r\n");

    if (StrLen(&r->hdrs))
    {
        StrCat(&r->resp_headers, StrPtr(&r->hdrs));
    }

    StrCat(&r->resp_headers,  "\r\n");

    if (r->resp_headers.err)
    {
        print__err(r, "<%s:%d> Error create response headers: %d\n", __func__, __LINE__, r->resp_headers.err);
        return -1;
    }
//fprintf(stderr, "-------------------------------------\n%s\n", StrPtr(&r->resp_headers));
    return 0;
}
//======================================================================
void create_html(Connect *r, const char *msg)
{
    const char *title = status_resp(r->respStatus);
    StrCpy(&r->html, "<html>\r\n"
                "<head><title>");
    StrCat(&r->html, title);
    StrCat(&r->html, "</title></head>\r\n"
                "<body>\r\n"
                "<h3>");
    StrCat(&r->html, title);
    StrCat(&r->html, "</h3>\r\n");
    if (msg)
    {
        StrCat(&r->html, "<p>");
        StrCat(&r->html, msg);
        StrCat(&r->html, "</p>\r\n");
    }
    StrCat(&r->html, "<hr>\r\n");
    StrCat(&r->html, r->sLogTime);
    StrCat(&r->html, "\r\n"
                "</body>\r\n"
                "</html>\r\n");
}
//======================================================================
int create_message(Connect *r, const char *msg)
{
    r->respContentType = "text/html";

    if (r->httpProt == 0)
        r->httpProt = HTTP11;

    StrReserve(&r->html, 512);
    create_html(r, msg);
    if (r->html.err)
    {
        print__err(r, "<%s:%d> Error create HTML file\n", __func__, __LINE__);
        return -1;
    }

    r->mode_send = NO_CHUNK;
    r->respContentLength = r->html.len;
    if (create_response_headers(r))
    {
        print__err(r, "<%s:%d> Error send_header_response()\n", __func__, __LINE__);
        return -1;
    }

    push_send_html(r);
    return 1;
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
