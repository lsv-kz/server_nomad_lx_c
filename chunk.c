#include "server.h"

//======================================================================
static int send_chunk(chunked *chk, int size)
{
    if (chk->err) return -1;
    char *p;
    int len;
    if (chk->mode == SEND_CHUNK)
    {
        char sTmp[8];
        snprintf(sTmp, sizeof(sTmp), "%X\r\n", size);
        len = strlen(sTmp);
        int n = MAX_LEN_SIZE_CHUNK - len;
        memcpy(chk->buf + n, sTmp, len);
        memcpy(chk->buf + chk->i, "\r\n", 2);
        chk->i += 2;
        p = chk->buf + n;
        len = chk->i - n;
    }
    else
    {
        p = chk->buf + MAX_LEN_SIZE_CHUNK;
        len = chk->i - MAX_LEN_SIZE_CHUNK;
    }
    
    int ret = write_timeout(chk->sock, p, len, conf->TIMEOUT);
    chk->i = MAX_LEN_SIZE_CHUNK;
    if (ret < 0)
    {
        chk->err = 1;
        return ret;
    }

    chk->allSend += ret;
    return ret;
}
//======================================================================
void chunk_add_longlong(chunked *chk, long long ll)
{
    if (chk->err) return;
    char s[21];
    snprintf(s, sizeof(s), "%lld", ll);
    int n = 0, len = strlen(s);
    if (chk->mode == NO_SEND)
    {
        chk->allSend += len;
        return;
    }
    
    while ((CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK) < (chk->i + len))
    {
        int l = CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i;
        memcpy(chk->buf + chk->i, s + n, l);
        chk->i += l;
        len -= l;
        n += l;
        int ret = send_chunk(chk, chk->i - MAX_LEN_SIZE_CHUNK);
        if (ret < 0)
        {
            chk->err = 1;
            return;
        }
    }

    memcpy(chk->buf + chk->i, s + n, len);
    chk->i += len;
}
//======================================================================
void va_chunk_add_str(chunked *chk, int numArg, ...)
{
    if (chk->err) return;
    va_list argptr;
    va_start(argptr, numArg);
    char *s;
    int i;
    for (i = 0; i < numArg; ++i)
    {
        s = va_arg(argptr, char*);
        int n = 0, len = strlen(s);
        if (chk->mode)
        {
            while ((CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK) < (chk->i + len))
            {
                int l = CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i;
                memcpy(chk->buf + chk->i, s + n, l);
                chk->i += l;
                len -= l;
                n += l;
                int ret = send_chunk(chk, chk->i - MAX_LEN_SIZE_CHUNK);
                if (ret < 0)
                {
                    chk->err = 1;
                    return;
                }
            }

            memcpy(chk->buf + chk->i, s + n, len);
            chk->i += len;
        }
        else
        {
            chk->allSend += len;
        }
    }

    va_end(argptr);
}
//======================================================================
void chunk_add_arr(chunked *chk, char *s, int len)
{
    if (chk->err) return;
    if (chk->mode == NO_SEND)
    {
        chk->allSend += len;
        return;
    }
    int n = 0;
    while ((CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK) < (chk->i + len))
    {
        int l = CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i;
        memcpy(chk->buf + chk->i, s + n, l);
        chk->i += l;
        len -= l;
        n += l;
        int ret = send_chunk(chk, chk->i - MAX_LEN_SIZE_CHUNK);
        if (ret < 0)
        {
            chk->err = 1;
            return;
        }
    }

    memcpy(chk->buf + chk->i, s + n, len);
    chk->i += len;
}
//======================================================================
void chunk_end(chunked *chk)
{
    if (chk->err) return;
    if (chk->mode == SEND_CHUNK)
    {
        int n = chk->i - MAX_LEN_SIZE_CHUNK;
        const char *s = "\r\n0\r\n";
        int len = strlen(s);
        memcpy(chk->buf + chk->i, s, len);
        chk->i += len;
        send_chunk(chk, n);
    }
    else if (chk->mode == SEND_NO_CHUNK)
        send_chunk(chk, 0);
}
//======================================================================
int cgi_to_client(chunked *chk, int fdPipe)
{
    if (chk->err) return -1;
    while (1)
    {
        if ((CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i) <= 0)
        {
            int ret = send_chunk(chk, chk->i - 6);
            if (ret < 0)
                return ret;
        }
        
        int rd = CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i;
        int ret = read_timeout(fdPipe, chk->buf + chk->i, rd, conf->TIMEOUT_CGI);
        if (ret == 0)
        {
            print_err("<%s:%d> ret=%d\n", __func__, __LINE__, ret);
            break;
        }
        else if (ret < 0)
        {
            chk->i = MAX_LEN_SIZE_CHUNK;
            chk->err = 1;
            return ret;
        }
        else if (ret != rd)
        {
            chk->i += ret;
            break;
        }
        else
        {
            chk->i += ret;
        }
    }
        
    return 0;
}
//======================================================================
int fcgi_to_client(chunked *chk, int fdPipe, int len)
{
    if (chk->err) return -1;
    if (chk->mode == NO_SEND)
    {
        chk->allSend += len;
        fcgi_to_cosmos(fdPipe, len, conf->TIMEOUT_CGI);
        return 0;
    }
    
    while (len > 0)
    {
        if ((CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i) <= 0)
        {
            int ret = send_chunk(chk, chk->i - MAX_LEN_SIZE_CHUNK);
            if (ret < 0)
            {
                chk->err = 1;
                return ret;
            }
        }
            
        int rd = (len < (CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i)) ? len : (CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK - chk->i);
        int ret = read_timeout(fdPipe, chk->buf + chk->i, rd, conf->TIMEOUT_CGI);
        if (ret == 0)
        {
            print_err("<%s:%d> ret=%d\n", __func__, __LINE__, ret);
            chk->i = MAX_LEN_SIZE_CHUNK;
            chk->err = 1;
            return -1;
        }
        else if (ret < 0)
        {
            chk->i = MAX_LEN_SIZE_CHUNK;
            chk->err = 1;
            return ret;
        }
        else if (ret != rd)
        {
            print_err("<%s:%d> ret != rd\n", __func__, __LINE__);
            chk->i = MAX_LEN_SIZE_CHUNK;
            chk->err = 1;
            return -1;
        }
        
        chk->i += ret;
        len -= ret;
    }
        
    return 0;
}
