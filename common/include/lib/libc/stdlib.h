#pragma once

#define malloc(size) (0)
#define free(ptr) \
    do { \
        (void)(ptr); \
    } while (0)
#ifndef NULL
#define NULL ((void *) 0)
#endif
int atexit(void (*func)(void));
void exit(int status);
int atoi(const char *str);

