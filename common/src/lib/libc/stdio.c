#include <stdio.h>
#include <unistd.h>

#ifdef BARE_METAL
int fflush(void *stream)
{
    (void)stream;
    return 0;
}
#endif

#ifndef BARE_METAL
int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1)
        return -1;
    return c;
}
#endif