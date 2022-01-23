#include "server.h"

enum {DIGIT = 1, DASH};
//======================================================================
int check_ranges(Connect *req)
{
    int size = req->numPart;
    struct Range *r = req->rangeBytes;

    for (int i = 0; i < size; i++)
    {
        if (r[i].part_len == 0)
                continue;

        for (int n = 0; n < size; n++)
        {
            if (i == n)
                continue;

            if (r[n].part_len == 0)
                continue;

            if (r[i].start >= r[n].start)
            {
                if (r[i].start <= (r[n].stop + 1))
                {
                    if (r[i].stop > r[n].stop)
                    {
                        r[n].stop = r[i].stop;
                        r[n].part_len = r[n].stop - r[n].start + 1;
                        r[i].part_len = 0;
                    }
                    else
                    {
                        r[i].part_len = 0;
                    }
                    break;
                }
            }
        }
    }

    int numPart = 0;
    for (int i = 0; i < size; i++)
    {
        if (r[i].part_len)
            numPart++;
    }

    if (numPart == size)
        return numPart;

    for (int i = 0; i < numPart; )
    {
        for ( ; i < numPart; i++)
        {
            if (r[i].part_len == 0)
                break;
        }
    
        for (int n = i + 1; (n < size) && (i < numPart); )
        {
            if (r[n].part_len != 0)
            {
                r[i].start = r[n].start;
                r[i].stop = r[n].stop;
                r[i].part_len = r[n].part_len;
                r[n].part_len = 0;

                break;
            }
            else
                n++;
        }
        i++;
    }
    
    req->numPart = numPart;
    return numPart;
}
//======================================================================
int parse_ranges(Connect *req)
{
    if (!req->sRange) return -RS416;
    int countStructs, len = strlen(req->sRange);
    long long start, stop, size = req->fileSize;

    req->numPart = 0;
    
    int inputIndex = 0;
    for (int ch; (ch = *(req->sRange + inputIndex)); inputIndex++)
    {
        if ((ch != ' ') && (ch != '\t'))
            break;
    }
    
    countStructs = 0;
    int minus = 0, blank = 0, num = 0;
    int i = inputIndex;
    while (1)
    {
        int ch = *(req->sRange + i);
        if ((ch == '-') && !blank)
        {
            if (minus > 0)
                break;
            minus++;
        }
        else if (((ch >= '0') && (ch <= '9')) && !blank)
        {
            if (!num)
                num = 1;
        }
        else if ((ch == '\0') || (ch == '\n') || (ch == '\r'))
        {
            if (minus && num)
                countStructs++;
            break;
        }
        else if ((ch == ' ') || (ch == '\t'))
        {
            if (!blank)
                blank = 1;
        }
        else if ((ch == ',') && !blank)
        {
            if (!minus || !num)
                break;
            countStructs++;
            minus = blank = num = 0;
        }
        else
        {
            break;
        }
        
        i++;
        if (i >= len)
        {
            if (minus && num)
            {
                countStructs++;
            }
            break;
        }
    }

    req->rangeBytes = malloc(sizeof(struct Range) * countStructs);
    if (!req->rangeBytes)
        return req->numPart;

    char sStart[16], sStop[16], *ptrStr;
    for (; (inputIndex < len) && (req->numPart < countStructs); )
    {
        int firstChar = 0;
        minus = blank = num = 0;
        ptrStr = sStart;
        for (int outputIndex = 0; outputIndex < 16; inputIndex++)
        {
            int ch = *(req->sRange + inputIndex);
            if ((ch == '-') && !blank)
            {
                if (minus > 0)
                    return req->numPart;
                if (!firstChar)
                    firstChar = DASH;
                minus++;
                ptrStr = sStop;
                outputIndex = 0;
            }
            else if (((ch >= '0') && (ch <= '9')) && !blank)
            {
                if (!firstChar)
                    firstChar = DIGIT;
                if (outputIndex == 0)
                    num++;

                ptrStr[outputIndex++] = ch;
                ptrStr[outputIndex] = 0;
            }
            else if ((ch == '\0') || (ch == '\n') || (ch == '\r'))
                break;
            else if ((ch == ' ') || (ch == '\t'))
            {
                if (!blank)
                    blank = 1;
                
                continue;
            }
            else if ((ch == ',') && !blank)
            {
                inputIndex++;           
                break;
            }
            else
                return req->numPart;
        }

        if (!minus || !num || (num > 2))
        {
            if (req->numPart > 0) return req->numPart;
            else return -RS416;
        }
        else if (num == 1 && firstChar == DASH)
        {
            sscanf(sStop, "%lld", &stop);
            start = size - stop;
            stop = size - 1;
        }
        else if (num == 1 && firstChar == DIGIT)
        {
            sscanf(sStart, "%lld", &start);
            stop = size - 1;
        }
        else if (num == 2  && firstChar == DIGIT)
        {
            sscanf(sStart, "%lld", &start);
            sscanf(sStop, "%lld", &stop);
        }
        else
            return req->numPart;

        if ((start > stop) || (start >= size) || (stop >= size) || (start < 0))
        {
            if (req->numPart > 0) return req->numPart;
            else return -RS416;
        }
        
        struct Range r = {start, stop, stop - start + 1};
        req->rangeBytes[req->numPart++] = r;
    }
    return req->numPart;
}
//======================================================================
int get_ranges(Connect *req)
{
    int n = parse_ranges(req);
    if (n > 1)
    {
        return (n = check_ranges(req));
    }
    else if (n == 1)
    {
        req->offset = req->rangeBytes[0].start;
        req->respContentLength = req->rangeBytes[0].part_len;
    }
        
    return n;
}
//======================================================================
void free_range(Connect *r)
{
    r->numPart = 0;
    if(r->rangeBytes)
    {
        free(r->rangeBytes);
        r->rangeBytes = NULL;
    }
}
