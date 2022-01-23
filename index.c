#include "server.h"

//======================================================================
int isimage(char *name)
{
    char *p;

    p = strrchr(name, '.');
    if (!p)
        return 0;

    if (!strlcmp_case(p, ".gif", 4)) return 1;
    else if (!strlcmp_case(p, ".png", 4)) return 1;
    else if (!strlcmp_case(p, ".ico", 4)) return 1;
    else if (!strlcmp_case(p, ".svg", 4)) return 1;
    else if (!strlcmp_case(p, ".jpeg", 5) || !strlcmp_case(p, ".jpg", 4)) return 1;
    return 0;
}
//======================================================================
int isaudiofile(char *name)
{
    char *p;

    if (!(p = strrchr(name, '.'))) return 0;

    if (!strlcmp_case(p, ".wav", 4)) return 1;
    else if (!strlcmp_case(p, ".mp3", 4)) return 1;
    else if (!strlcmp_case(p, ".ogg", 4)) return 1;
    return 0;
}
//======================================================================
int cmp(const void *a, const void *b)
{
    unsigned int n1, n2;
    int i;

    if ((n1 = str_num(*(char **)a)) > 0)
    {
        if ((n2 = str_num(*(char **)b)) > 0)
        {
            if (n1 < n2) i = -1;
            else if (n1 == n2)
                i = strcmp(*(char **)a, *(char **)b);
            else i = 1;
        }
        else i = strcmp(*(char **)a, *(char **)b);
    }
    else i = strcmp(*(char **)a, *(char **)b);

    return i;
}
//======================================================================
int index_chunked(Connect *req, String *hdrs, char **list, int numFiles)
{
    const int len_path = str_len(req->path);
    struct stat st;
    int chunk;
    if (req->reqMethod == M_HEAD)
        chunk = NO_SEND;
    else
        chunk = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;
    
    chunked chk = {MAX_LEN_SIZE_CHUNK, chunk, 0, req->clientSocket};

    req->respStatus = RS200;
    req->numPart = 0;
    
    if (chunk == SEND_CHUNK)
    {
        str_cat(hdrs, "Transfer-Encoding: chunked\r\n");
    }

    str_cat(hdrs, "Content-Type: text/html\r\n");
    req->respContentLength = -1;
    
    if (chunk)
    {
        if (send_response_headers(req, hdrs))
        {
            print__err(req, "<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }
    }

    va_chunk_add_str(&chk, 2, "<!DOCTYPE HTML>\n"
        "<html>\n"
        " <head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>Index of ", req->decodeUri);
    if (chk.err)
    {
        print_err("<%s:%d> Error chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    //------------------------------------------------------------------
    va_chunk_add_str(&chk, 3, "</title>\n"
        "  <style>\n"
        "    body {\n"
        "     margin-left:100px; margin-right:50px;\n"
        "    }\n"
        "  </style>\n"
        "  <link href=\"/styles.css\" type=\"text/css\" rel=\"stylesheet\">"
        " </head>\n"
        " <body id=\"top\">\n"
        "  <h3>Index of ", 
        req->decodeUri, 
        "</h3>\n"
        "  <table cols=\"2\" width=\"100\%\">\n"
        "   <tr><td><h3>Directories</h3></td><td></td></tr>\n");
    
    if (chk.err)
    {
        print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    //------------------------------------------------------------------
    if (!strcmp(req->decodeUri, "/"))
        va_chunk_add_str(&chk, 1, "   <tr><td></td><td></td></tr>\n");
    else
        va_chunk_add_str(&chk, 1, "   <tr><td><a href=\"../\">Parent Directory/</a></td><td></td></tr>\n");
    if (chk.err)
    {
        print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    //-------------------------- Directories ---------------------------
    for (int i = 0; (i < numFiles); i++)
    {
        char buf[1024];
        str_cat(req->path, list[i]);
        if (req->path->err)
        {
            print__err(req, "<%s:%d> Error: Buffer Overflow: %s\n", __func__, __LINE__, list[i]);
            continue;
        }
        
        int n = lstat(Str(req->path), &st);
        str_resize(req->path, len_path);
        if ((n == -1) || !S_ISDIR (st.st_mode))
            continue;

        if (!encode(list[i], buf, sizeof(buf)))
        {
            print__err(req, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        va_chunk_add_str(&chk, 5, "   <tr><td><a href=\"", buf, "/\">", list[i], "/</a></td><td align=right></td></tr>\n");
        if (chk.err)
        {
            print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
            return -1;
        }
    }

    va_chunk_add_str(&chk, 1, "   <tr><td><hr></td><td><hr></td></tr>\n"
                "   <tr><td><h3>Files</h3></td><td></td></tr>\n");
    if (chk.err)
    {
        print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    //---------------------------- Files -------------------------------
    for (int i = 0; i < numFiles; i++)
    {
        char buf[1024];
        str_cat(req->path, list[i]);
        if (req->path->err)
        {
            print_err("<%s:%d> Error: Buffer Overflow: %s\n", __func__, __LINE__, list[i]);
            continue;
        }
            
        int n = lstat(Str(req->path), &st);
        str_resize(req->path, len_path);
        if ((n == -1) || (!S_ISREG (st.st_mode)))
            continue;
        
        if (!encode(list[i], buf, sizeof(buf)))
        {
            print__err(req, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        long long size = (long long)st.st_size;

        if (isimage(list[i]) && (conf->ShowMediaFiles == 'y'))
        {
            if (size < 8000LL)
            {
                va_chunk_add_str(&chk, 7, "   <tr><td><a href=\"", buf, "\"><img src=\"", buf, "\"></a><br>", 
                                                list[i], "</td><td align=\"right\">");
                chunk_add_longlong(&chk, size);
                va_chunk_add_str(&chk, 1, " bytes</td></tr>\n   <tr><td></td><td></td></tr>\n");
                
                if (chk.err)
                {
                    print_err("<%s:%d> Error chunk_add_str()\n", __func__, __LINE__);
                    return -1;
                }
            }
            else
            {
                va_chunk_add_str(&chk, 7, "   <tr><td><a href=\"", buf, "\"><img src=\"", buf, "\" width=\"300\"></a><br>", 
                                        list[i], "</td><td align=\"right\">");
                chunk_add_longlong(&chk, size);
                va_chunk_add_str(&chk, 1, " bytes</td></tr>\n   <tr><td></td><td></td></tr>\n");
                
                if (chk.err)
                {
                    print_err("<%s:%d> Error chunk_add_str()\n", __func__, __LINE__);
                    return -1;
                }
            }
            
        }
        else if (isaudiofile(list[i]) && (conf->ShowMediaFiles == 'y'))
        {
            va_chunk_add_str(&chk, 7, "   <tr><td><audio preload=\"none\" controls src=\"", buf, 
                                "\"></audio><a href=\"", buf, "\">", list[i], "</a></td><td align=\"right\">");
            chunk_add_longlong(&chk, size);
            va_chunk_add_str(&chk, 1, " bytes</td></tr>\n");
            if (chk.err)
            {
                print_err("<%s:%d> Error chunk_add_str()\n", __func__, __LINE__);
                return -1;
            }
        }
        else
        {
            va_chunk_add_str(&chk, 5, "   <tr><td><a href=\"", buf, "\">", list[i], "</a></td><td align=\"right\">");
            chunk_add_longlong(&chk, size);
            va_chunk_add_str(&chk, 1, " bytes</td></tr>\n");
            
            if (chk.err)
            {
                print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
                return -1;
            }
        }
    }
    //------------------------------------------------------------------
    va_chunk_add_str(&chk, 3, 
            "  </table>\n"
            "  <hr>\n  ", 
            req->sLogTime,
            "\n  <a href=\"#top\" style=\"display:block;\n"
            "         position:fixed;\n"
            "         bottom:30px;\n"
            "         left:10px;\n"
            "         width:50px;\n"
            "         height:40px;\n"
            "         font-size:60px;\n"
            "         background:gray;\n"
            "         border-radius:10px;\n"
            "         color:black;\n"
            "         opacity: 0.7\">^</a>\n"
            " </body>\n"
            "</html>");
    if (chk.err)
    {
        print_err("<%s:%d> Error va_chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    
    chunk_end(&chk);
    req->respContentLength = chk.allSend;
    if (chk.err)
    {
        print_err("<%s:%d> Error chunk_add_str()\n", __func__, __LINE__);
        return -1;
    }
    
    if (chunk == NO_SEND)
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
int read_dir(Connect *req)
{
    DIR *dir;
    struct dirent *dirbuf;
    int maxNumFiles = 1024, numFiles = 0;
    char *list[maxNumFiles];
    int ret;

    dir = opendir(Str(req->path));
    if (dir == NULL)
    {  
        if (errno == EACCES)
            return -RS403;
        else
        {
            print__err(req, "<%s:%d> Error opendir(\"%s\"): %s\n", __func__, __LINE__, Str(req->path), str_err(errno));
            return -RS500;
        }
    }
    
    while ((dirbuf = readdir(dir)))
    {        
        if (numFiles >= maxNumFiles )
        {
            print__err(req, "<%s:%d> number of files per directory >= %d\n", __func__, __LINE__, numFiles);
            break;
        }
        
        if (dirbuf->d_name[0] == '.')
            continue;
        list[numFiles] = dirbuf->d_name;
        ++numFiles;
    }

    qsort(list, numFiles, sizeof(char *), cmp);
    String hdrs = str_init(200);
    if (hdrs.err == 0)
    {
        ret = index_chunked(req, &hdrs, list, numFiles);
        str_free(&hdrs);
    }
    else
    {
        print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
        ret = -1;
    }

    closedir(dir);

    return ret;
}
