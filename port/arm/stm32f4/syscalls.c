/**
 * @file syscalls.c
 * @brief An implementation of newlib stubs.
 * The syscalls such as `_close`, `_fstat`, `_isatty`, and `_lseek` are stubbed out to
 * satisfy the linker requirements of newlib, but they do not perform any actual operations.
 * The `_read` and `_write` syscalls are implemented to redirect standard input and
 * output to the UART interface, allowing for console I/O functionality.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fifo.h"

// Stubs to override the warnings from libc_nano and libnosys

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/**
 * @brief Terminal check syscall.
 */
int _isatty(int file)
{
    if (file == STDIN_FILENO || file == STDOUT_FILENO || file == STDERR_FILENO)
        return 1;
    return 0;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/**
 * @brief Low-level write syscall for all standard output.
 */
int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO) {
        for (int i = 0; i < len; i++) {
            putchar(ptr[i]);
        }
        return len;
    }
    return -1;
}

/**
 * @brief Low-level read syscall.
 * Redirects standard input STDIN_FILENO (0) to UART FIFO.
 */
int _read(int file, char *ptr, int len)
{
    if (file != STDIN_FILENO)
        return 0;

    int bytes_read = 0;

    while (bytes_read < len) {
        if (fifo_is_empty())
            break;

        int c = fifo_pop();
        if (c == -1)
            break;

        *ptr++ = (char)c;
        bytes_read++;
    }

    return bytes_read;
}
