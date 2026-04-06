#include <stdio.h>
#include <unistd.h>

#ifndef BARE_METAL
int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1)
        return -1;
    return c;
}

int getchar(void)
{
    char ch;
    long result = read(STDIN_FILENO, &ch, 1);
    if (result == 1)
        return (int)ch;
    else
        return EOF;
}
#endif

int atoi(const char *s)
{
    int num = 0;
    while (*s >= '0' && *s <= '9') {
        num = num * 10 + (*s++ - '0');
    }
    return num;
}
