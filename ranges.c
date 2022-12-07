#include "server.h"

//======================================================================
int check_ranges(Connect *req)
{
    int Len = req->numPart;
    Range *r = req->rangeBytes;

    for ( int n = Len - 1; n > 0; n--)
    {
        for (int i = n - 1; i >= 0; i--)
        {
            if (((r[n].end + 1) >= r[i].start) && ((r[i].end + 1) >= r[n].start))
            {
                if (r[n].start < r[i].start)
                    r[i].start = r[n].start;

                if (r[n].end > r[i].end)
                    r[i].end = r[n].end;

                r[i].len = r[i].end - r[i].start + 1;
                r[n].len = 0;

                n--;
                req->numPart--;
            }
        }
    }

    for (int i = 0, j = 0; j < Len; j++)
    {
        if (r[j].len)
        {
            if (i < j)
            {
                r[i].start = r[j].start;
                r[i].end = r[j].end;
                r[i].len = r[j].len;
                r[j].len = 0;
            }

            i++;
        }
    }

    return req->numPart;
}
//======================================================================
int parse_ranges(Connect *req, int Len)
{
    long long start = 0, end = 0, size = req->fileSize, ll;
    int i = 0;
    const char *p1;
    char *p2;

    req->numPart = 0;

    p1 = p2 = req->sRange;

    for ( ; req->numPart < Len; )
    {
        if ((*p1 >= '0') && (*p1 <= '9'))
        {
            ll = strtoll(p1, &p2, 10);
            if (p1 < p2)
            {
                if (i == 0)
                    start = ll;
                else if (i == 2)
                    end = ll;
                else
                {
                    fprintf(stderr, "<%s:%d> \"416 Requested Range Not Satisfiable\"\n", __func__, __LINE__);
                    return -416;
                }

                i++;
                p1 = p2;
            }
        }
        else if (*p1 == ' ')
            p1++;
        else if (*p1 == '-')
        {
            if (i == 0)
            {
                ll = strtoll(p1, &p2, 10);
                if (ll < 0)
                {
                    start = size + ll;
                    end = size - 1;
                    i = 3;
                    p1 = p2;
                }
                else
                {
                    fprintf(stderr, "<%s:%d> \"416 Requested Range Not Satisfiable\"\n", __func__, __LINE__);
                    return -416;
                }
            }
            else if (i == 2)
            {
                fprintf(stderr, "<%s:%d> \"416 Requested Range Not Satisfiable\"\n", __func__, __LINE__);
                return -416;
            }
            else
            {
                p1++;
                i++;
            }
        }
        else if ((*p1 == ',') || (*p1 == 0))
        {
            if (i == 2)
                end = size - 1;
            else if (i != 3)
            {
                fprintf(stderr, "<%s:%d> \"416 Requested Range Not Satisfiable\"\n", __func__, __LINE__);
                return -416;
            }

            if (end >= size)
                end = size - 1;

            if (start <= end)
            {
                req->rangeBytes[req->numPart++] = (Range){start, end, end - start + 1};
                if (*p1 == 0)
                    break;
                start = end = 0;
                p1++;
                i = 0;
            }
            else
                return -416;
        }
        else
        {
            fprintf(stderr, "<%s:%d> \"416 Requested Range Not Satisfiable\"\n", __func__, __LINE__);
            return -416;
        }
    }

    return req->numPart;
}
//======================================================================
int get_ranges(Connect *req)
{
    int SizeArray = 0;

    if (!req->sRange)
        return -RS500;

    if (conf->MaxRanges == 0)
        return -RS403;

    for ( char *p = req->sRange; *p; ++p)
    {
        if (*p == ',')
            SizeArray++;
    }

    SizeArray++;
    if (SizeArray > conf->MaxRanges)
        SizeArray = conf->MaxRanges;

    req->rangeBytes = malloc(sizeof(Range) * SizeArray);
    if (!req->rangeBytes)
        return -RS500;

    int n = parse_ranges(req, SizeArray);
    //if (n > 1)
    //    return (n = check_ranges(req));
    return n;
}
//======================================================================
void free_range(Connect *r)
{
    r->numPart = 0;
    if (r->rangeBytes)
    {
        free(r->rangeBytes);
        r->rangeBytes = NULL;
    }
}
