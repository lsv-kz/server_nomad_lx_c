#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

//======================================================================
int main()
{
    fflush(stdin);
    fflush(stdout);
    
    printf("Content-Type: text/plain\r\n"
            "Pragma: no-cache\r\n"
            "\r\n"
            "1. Hello from script;\n"
            "11111111111111111111111111111111111111111111111111111111111\n"
            "22222222222222222222233333333333333333333333333333333333333\n"
            "44444444444444444444444444444444444444444444444444444444444\n"
            "55555555555555555555555555555555555555555555555555555555555\n"
            "66666666666666666666666666666666666666666666666666666666666"
    );

    return 0;
}
