#pragma once

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef __SIZE_TYPE__ size_t;

#ifdef __cplusplus
extern "C" {
#endif
__attribute__((weak)) void *malloc(size_t size);
__attribute__((weak)) void  free(void *ptr);
int                         atexit(void (*func)(void));
void                        exit(int status);
int                         atoi(const char *str);
#ifdef __cplusplus
}
#endif
