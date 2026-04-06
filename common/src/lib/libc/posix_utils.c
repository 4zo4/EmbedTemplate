#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>

// Manually define the poll structure to avoid including poll.h
struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN 0x0001
#define STDIN_FILENO 0

// Manual prototype for syscall to avoid unistd.h poisoning
long syscall(long number, ...);

bool stdin_ready(int timeout_ms)
{
    struct pollfd fds;
    fds.fd = STDIN_FILENO;
    fds.events = POLLIN;
    fds.revents = 0;

    // Use the raw syscall: syscall(SYS_poll, fds, nfds, timeout)
    // This is identical to calling poll() but bypasses the glibc headers
    int ret = syscall(SYS_poll, &fds, 1, timeout_ms);

    return (ret > 0 && (fds.revents & POLLIN));
}