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
int create_index_html(Connect *req, char **list, int numFiles)
{
    const int len_path = StrLen(&req->path);
    struct stat st;

    req->respStatus = RS200;

    StrCat(&req->hdrs, "Content-Type: text/html\r\n");
    req->respContentLength = -1;

    StrCat(&req->html, "<!DOCTYPE HTML>\n"
        "<html>\n"
        " <head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>Index of ");
    StrCat(&req->html, req->decodeUri);
    //------------------------------------------------------------------
    StrCat(&req->html, "</title>\n"
        "  <style>\n"
        "    body {\n"
        "     margin-left:100px; margin-right:50px;\n"
        "    }\n"
        "  </style>\n"
        "  <link href=\"/styles.css\" type=\"text/css\" rel=\"stylesheet\">\n"
        " </head>\n"
        " <body id=\"top\">\n"
        "  <h3>Index of ");
    StrCat(&req->html, req->decodeUri);
    StrCat(&req->html,
        "</h3>\n"
        "  <table cols=\"2\" width=\"100\%\">\n"
        "   <tr><td><h3>Directories</h3></td><td></td></tr>\n");
    //------------------------------------------------------------------
    if (!strcmp(req->decodeUri, "/"))
        StrCat(&req->html, "   <tr><td></td><td></td></tr>\n");
    else
        StrCat(&req->html, "   <tr><td><a href=\"../\">Parent Directory/</a></td><td></td></tr>\n");
    //-------------------------- Directories ---------------------------
    for (int i = 0; (i < numFiles); i++)
    {
        char buf[1024];
        StrCat(&req->path, list[i]);
        if (req->path.err)
        {
            print__err(req, "<%s:%d> Error: Buffer Overflow: %s\n", __func__, __LINE__, list[i]);
            continue;
        }

        int n = lstat(req->path.ptr, &st);
        StrResize(&req->path, len_path);
        if ((n == -1) || !S_ISDIR (st.st_mode))
            continue;

        if (!encode(list[i], buf, sizeof(buf)))
        {
            print__err(req, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        StrCat(&req->html, "   <tr><td><a href=\"");
        StrCat(&req->html, buf);
        StrCat(&req->html, "/\">");
        
        StrCat(&req->html, list[i]);
        StrCat(&req->html, "/</a></td><td align=right></td></tr>\n");
    }

    StrCat(&req->html, "   <tr><td><hr></td><td><hr></td></tr>\n"
                "   <tr><td><h3>Files</h3></td><td></td></tr>\n");
    //---------------------------- Files -------------------------------
    for (int i = 0; i < numFiles; i++)
    {
        char buf[1024];
        StrCat(&req->path, list[i]);
        if (req->path.err)
        {
            print_err("<%s:%d> Error: Buffer Overflow: %s\n", __func__, __LINE__, list[i]);
            continue;
        }

        int n = lstat(req->path.ptr, &st);
        StrResize(&req->path, len_path);
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
            StrCat(&req->html, "   <tr><td><a href=\"");
            StrCat(&req->html, buf);
            StrCat(&req->html, "\"><img src=\"");
            StrCat(&req->html, buf);
            StrCat(&req->html, "\" width=\"100\"></a><br>");
            
            StrCat(&req->html, list[i]);
            StrCat(&req->html, "</td><td align=\"right\">");
            StrCatInt(&req->html, size);
            StrCat(&req->html, " bytes</td></tr>\n   <tr><td></td><td></td></tr>\n");
        }
        else if (isaudiofile(list[i]) && (conf->ShowMediaFiles == 'y'))
        {
            StrCat(&req->html, "   <tr><td><audio preload=\"none\" controls src=\"");
            StrCat(&req->html, buf);
            StrCat(&req->html, "\"></audio><a href=\"");
            StrCat(&req->html, buf);
            StrCat(&req->html, "\">");
            StrCat(&req->html, list[i]);
            StrCat(&req->html, "</a></td><td align=\"right\">");
            StrCatInt(&req->html, size);
            StrCat(&req->html, " bytes</td></tr>\n");
        }
        else
        {
            StrCat(&req->html, "   <tr><td><a href=\"");
            StrCat(&req->html, buf);
            StrCat(&req->html, "\">");
            StrCat(&req->html, list[i]);
            StrCat(&req->html, "</a></td><td align=\"right\">");
            StrCatInt(&req->html, size);
            StrCat(&req->html, " bytes</td></tr>\n");
        }
    }
    //------------------------------------------------------------------
    StrCat(&req->html, 
            "  </table>\n"
            "  <hr>\n  ");
    StrCat(&req->html, req->sLogTime);
    StrCat(&req->html,
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
    if (req->html.err)
    {
        print_err("<%s:%d> Error create_index_html()\n", __func__, __LINE__);
        return -1;
    }

    req->respContentLength = req->html.len;

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

    if (req->reqMethod == M_POST)
        return -RS405;

    StrReserve(&req->hdrs, 200);
    if (req->hdrs.err)
    {
        print__err(req, "<%s:%d> Error: malloc()\n", __func__, __LINE__);
        return -1;
    }

    dir = opendir(req->path.ptr);
    if (dir == NULL)
    {
        if (errno == EACCES)
            return -RS403;
        else
        {
            print__err(req, "<%s:%d> Error opendir(\"%s\"): %s\n", __func__, __LINE__, req->path.ptr, strerror(errno));
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
    ret = create_index_html(req, list, numFiles);
    closedir(dir);
    if (ret >= 0)
    {
        if (create_response_headers(req))
        {
            print__err(req, "<%s:%d> Error create_response_headers()\n", __func__, __LINE__);
            return -1;
        }
        
        push_send_html(req);
        return 1;
    }

    return ret;
}
