#pragma once

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

long write(int fd, const void *buf, unsigned long count);
long read(int fd, const void *buf, unsigned long count);
