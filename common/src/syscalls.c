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

#include "arch_ops.h"
#include "fifo.h"

// Stubs to override the warnings from libc_nano and libnosys

__attribute__((weak)) int _close(int file)
{
    (void)file;
    return -1;
}

__attribute__((weak)) int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/**
 * @brief Terminal check syscall.
 */
__attribute__((weak)) int _isatty(int file)
{
    if (file == STDIN_FILENO || file == STDOUT_FILENO || file == STDERR_FILENO)
        return 1;
    return 0;
}

__attribute__((weak)) int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/**
 * @brief Low-level write syscall for all standard output.
 */
__attribute__((weak)) int _write(int file, char *ptr, int len)
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
__attribute__((weak)) int _read(int file, char *ptr, int len)
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

/**
 * @brief Low-level exit syscall.
 */
__attribute__((weak)) __attribute__((noreturn)) void _exit(int status)
{
    (void)status;
    while (true) {
        HALT_CPU();
    }
}

void exit(int status) {
    (void)status;
    _exit(status);
}

/**
 * @brief Stub for atexit.
 */
__attribute__((weak)) int atexit(void (*function)(void))
{
    (void)function;
    return 0;
}

__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
    extern char  _end;
    static char *heap_ptr;
    char        *prev_heap_ptr;

    if (heap_ptr == 0)
        heap_ptr = &_end;
    prev_heap_ptr = heap_ptr;
    heap_ptr += incr;
    return (void *)prev_heap_ptr;
}

__attribute__((weak)) int _getpid(void)
{
    return 1;
}

__attribute__((weak)) int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

__attribute__((weak)) void free(void *ptr)
{
    (void)ptr;
}

__attribute__((weak)) void *malloc(size_t size)
{
    (void)size;
    return (void *)0;
}

// Dummy FILE structures for stdin, stdout, and stderr (works for newlib and picolibc)
#undef stdin
#undef stdout
#undef stderr
FILE *const stdin = (void*)0;
FILE *const stdout = (void*)0;
FILE *const stderr = (void*)0;
