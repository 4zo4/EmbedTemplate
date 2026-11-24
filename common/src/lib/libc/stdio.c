#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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
#endif // !BARE_METAL

int atoi(const char *s)
{
    int num = 0;
    while (*s >= '0' && *s <= '9') {
        num = num * 10 + (*s++ - '0');
    }
    return num;
}

#ifdef BARE_METAL

char *strcpy(char *dst, const char *src)
{
    size_t len = strlen(src) + 1;
    return memcpy(dst, src, len);
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char  *d = dst + strlen(dst);
    size_t i = 0;

    while (i < n && src[i] != '\0') {
        d[i] = src[i];
        i++;
    }
    d[i] = '\0';
    return dst;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, (size_t)0x7fffffff, fmt, args);
    va_end(args);
    return n;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char        ch = (unsigned char)c;

    while (n-- > 0) {
        if (*p == ch) {
            return (void *)p;
        }
        p++;
    }

    return nullptr;
}

#endif // BARE_METAL
