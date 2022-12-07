#include "server.h"

//----------------------------------------------------------------------
/*typedef struct {
    unsigned int len;
    unsigned int size;
    int err;
    char *ptr;
} String;*/
//======================================================================
String str_init(unsigned int n)
{
    String tmp;
    tmp.len = tmp.err = 0;
    tmp.size = n;
    if (n)
    {
        tmp.ptr = malloc(tmp.size);
        if (!tmp.ptr) tmp.err = 1;
    }
    else
        tmp.ptr = NULL;
    return tmp;
}
//======================================================================
void str_free(String *s)
{
    if (!s) return;
    if (s->ptr)
    {
        free(s->ptr);
        s->ptr = NULL;
    }
    s->size = s->len = s->err = 0;
}
//======================================================================
void str_reserve(String *s, unsigned int n)
{
    if (!s) return;
    if ((n <= s->size) || s->err)
        return;

    char *new_ptr = malloc(++n);
    if (!new_ptr)
    {
        s->err = 1;
        return;
    }

    if (s->len && s->ptr)
    {
        memcpy(new_ptr, s->ptr, s->len);
        free(s->ptr);
    }
    s->ptr = new_ptr;
    s->size = n;
}
//======================================================================
void str_clear(String *s)
{
    if (!s) return;
    s->size = s->len = s->err = 0;
    if (s->ptr)
    {
        free(s->ptr);
        s->ptr = NULL;
    }
}
//======================================================================
void str_resize(String *s, unsigned int n)
{
    if (!s) return;
    if (s->err || (!s->ptr)) return;
    if (n >= s->len) return;
    s->len = n;
}
//======================================================================
void str_cpy(String *s, const char *cs)
{
    if (!s || !cs) return;
    if (s->err) return;

    s->len = strlen(cs);
    if (s->len == 0)
        return;

    if (s->size <= s->len)
    {
        str_reserve(s, s->len + 1);
        if (s->err)
            return;
    }

    memcpy(s->ptr, cs, s->len);
}
//======================================================================
void str_cat(String *s, const char *cs)
{
    if (!s || !cs) return;
    if (s->err) return;

    unsigned int len = strlen(cs);
    if (len == 0)
        return;
    if (s->size <= (s->len + len))
    {
        str_reserve(s, s->len + len + 1);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
}
//======================================================================
void str_cat_ln(String *s, const char *cs)
{
    if (!s || s->err) return;
    if (!cs)
    {
        str_cat(s, "\r\n");
        return;
    }

    unsigned int len = strlen(cs);
    if (len == 0)
        return;
    if (s->size <= (s->len + len + 2))
    {
        str_reserve(s, s->len + len + 2 + 1);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
    memcpy(s->ptr + s->len, "\r\n", 2);
    s->len += 2;
}
//======================================================================
void str_n_cat(String *s, const char *cs, unsigned int len)
{
    if (!s || !cs) return;
    if (s->err || !len) return;
    if (s->size <= (s->len + len))
    {
        str_reserve(s, s->len + len + 1);
        if (s->err)
            return;
    }

    memcpy(s->ptr + s->len, cs, len);
    s->len += len;
}
//======================================================================
void str_llint(String *s, long long ll)
{
    if (!s || s->err) return;
    char buf[21];
    snprintf(buf, sizeof(buf), "%lld", ll);
    str_cat(s, buf);
}
//======================================================================
void str_llint_ln(String *s, long long ll)
{
    if (!s || s->err) return;
    char buf[21];
    snprintf(buf, sizeof(buf), "%lld", ll);
    str_cat_ln(s, buf);
}
//======================================================================
const char *str_ptr(String *s)
{
    if (!s || s->err || (!s->ptr)) return "";
    *(s->ptr + s->len) = 0;
    return s->ptr;
}
//======================================================================
int str_len(String *s)
{
    if (!s || s->err) return 0;
    return s->len;
}
