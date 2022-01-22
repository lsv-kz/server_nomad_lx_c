/*
 */
#include "server.h"

//======================================================================
int encode(const char *s_in, char *s_out, int len_out)
{
    unsigned char c,d;
    int cnt_o = 0;
    char *p = s_out;
    char Az09[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz" "0123456789" "/:-_.!~*'()";

    if((!s_in) || (!s_out))
        return 0;
    
    while((c = *s_in++))
    {
        if(c <= 0x7f)
        {
            if(!strchr(Az09, c))
            {
                if((cnt_o + 3) < len_out)
                {
                    *p++ = '%';
                    d = c >> 4;
                    *p++ = d < 10 ? d + '0' : d + '7';
                    d = c & 0x0f;
                    *p++ = d < 10 ? d + '0' : d + '7';
                    cnt_o += 3;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
            else if(c == ' ')
            {
                if((cnt_o + 1) < len_out)
                {
                    *p++ = '+';
                    cnt_o++;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
            else
            {
                if((cnt_o + 1) < len_out)
                {
                    *p++ = c;
                    cnt_o++;
                }
                else
                {
                    *s_out = 0;
                    return 0;
                }
            }
        }
        else
        {
            if((cnt_o + 3) < len_out)
            {
                *p++ = '%';
                d = c >> 4;
                *p++ = d < 10 ? d + '0' : d + '7';
                d = c & 0x0f;
                *p++ = d < 10 ? d + '0' : d + '7';
                cnt_o += 3;
            }
            else
            {
                *s_out = 0;
                return 0;
            }
        }
    }
    if(s_out)
        *p = 0;

    return cnt_o;
}
//======================================================================
int decode(char *s_in, int len_in, char *s_out, int len)
{
    char tmp[3];
    char *p = s_out;
    char hex[] = "0123456789ABCDEFabcdef";
    unsigned char c;

    int cnt = 0, i;

    while(len >= 1)
    {
        c = *(s_in++);
        cnt++;
        if(c == '%')
        {
            if (!strchr(hex, *s_in))
            {
                if(s_out)
                {
                    *p++ = c;
                    len--;
                }
            }
            else
            {
                tmp[0] = *(s_in++);
                --len_in;
                
                if (!len_in)
                    break;
                
                tmp[1] = *(s_in++);
                --len_in;
                if (!len_in)
                    break;
                
                tmp[2] = 0;
                
                if(strspn(tmp, hex) != 2)
                {
                    if(s_out)
                        *p = 0;
                    return 0;
                }
                
                sscanf(tmp, "%x", &i);
                
                if(s_out)
                {
                    *p++ = (char)i;
                    len--;
                }
            }
        }
        else if(c == '+')
        {
            if(s_out)
            {
                *p++ = ' ';
                len--;
            }
        }
        else
        {
            if(s_out)
            {
                *p++ = c;
                len--;
            }
        }
        
        --len_in;
        if (!len_in)
            break;
    }
    if(s_out)
        *p = 0;
    
    return cnt;
}
