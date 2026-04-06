#pragma once

int fflush(void *stream);
int getchar(void);
int putchar(int c);

#define sprintf(buf, fmt, ...) snprintf(buf, 64, fmt, ##__VA_ARGS__)