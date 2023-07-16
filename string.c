#include "server.h"

//----------------------------------------------------------------------
/*typedef struct {
    unsigned int len;
    unsigned int ind;
    unsigned int size;
    int err;
    char *ptr;
} String;*/
static const int RESERVE = 128;
//======================================================================
void StrInit(String *s)
{
    s->size = s->len = s->ind = s->err = 0;
    s->ptr = NULL;
}
//======================================================================
void StrFree(String *s)
{
    if (!s) return;
    if (s->ptr)
    {
        free(s->ptr);
        s->ptr = NULL;
    }
    s->size = s->len = s->ind = s->err = 0;
}
//======================================================================
void StrClear(String *s)
{
    if (!s) return;
    s->size = s->len = s->ind = s->err = 0;
}
//======================================================================
void StrReserve(String *s, unsigned int n)
{
    if (!s) return;
    ++n;
    if ((n <= s->size) || s->err)
        return;
    char *new_ptr = malloc(n);
    if (!new_ptr)
    {
        s->err = ENOMEM;
        return;
    }

    if (s->ptr)
    {
        if (s->len)
            memcpy(new_ptr, s->ptr, s->len);
        free(s->ptr);
    }
    s->ptr = new_ptr;
    s->size = n;
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrResize(String *s, unsigned int n)
{
    if (!s) return;
    if (s->err || (!s->ptr)) return;
    if (n >= s->len) return;
    s->len = n;
    *(s->ptr + s->len) = 0;
}
//======================================================================
int StrLen(String *s)
{
    if (!s || s->err) return 0;
    return s->len;
}
//======================================================================
int StrSize(String *s)
{
    if (!s || s->err) return 0;
    return s->size;
}
//======================================================================
const char *StrPtr(String *s)
{
    if (!s || s->err || (!s->ptr)) return NULL;
    *(s->ptr + s->len) = 0;
    return s->ptr;
}
//======================================================================
void StrCpy(String *s, const char *cs)
{
    if (!s || !cs) return;
    if (s->err) return;

    s->len = strlen(cs);
    if (s->len == 0)
        return;

    if (s->size <= s->len)
    {
        StrReserve(s, s->len + RESERVE);
        if (s->err)
            return;
    }

    memcpy(s->ptr, cs, s->len);
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrCpyLN(String *s, const char *cs)
{
    if (!s || !cs) return;
    if (s->err) return;

    s->len = strlen(cs);
    if (s->len == 0)
        return;

    if (s->size <= s->len)
    {
        StrReserve(s, s->len + RESERVE);
        if (s->err)
            return;
    }

    memcpy(s->ptr, cs, s->len);
    memcpy(s->ptr + s->len, "\r\n", 2);
    s->len += 2;
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrCat(String *s, const char *cs)
{
    if (!s || !cs) return;
    if (s->err) return;

    unsigned int len = strlen(cs);
    if (len == 0)
        return;
    if (s->size <= (s->len + len))
    {
        StrReserve(s, s->len + len + RESERVE);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrCatLN(String *s, const char *cs)
{
    if (!s || s->err) return;
    if (!cs)
    {
        StrCat(s, "\r\n");
        return;
    }

    unsigned int len = strlen(cs);
    if (len == 0)
        return;
    if (s->size <= (s->len + len + 2))
    {
        StrReserve(s, s->len + len + RESERVE);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
    memcpy(s->ptr + s->len, "\r\n", 2);
    s->len += 2;
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrnCat(String *s, const char *cs, unsigned int len)
{
    if (!s || !cs) return;
    if (s->err || !len) return;
    if (s->size <= (s->len + len))
    {
        StrReserve(s, s->len + len + RESERVE);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
    *(s->ptr + s->len) = 0;
}
//======================================================================
void StrCatInt(String *s, long long ll)
{
    if (!s || s->err) return;
    char buf[21];
    snprintf(buf, sizeof(buf), "%lld", ll);
    StrCat(s, buf);
}
//======================================================================
void StrCatIntLN(String *s, long long ll)
{
    if (!s || s->err) return;
    char buf[21];
    snprintf(buf, sizeof(buf), "%lld", ll);
    StrCatLN(s, buf);
}
