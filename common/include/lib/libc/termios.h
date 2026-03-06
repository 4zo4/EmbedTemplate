#pragma once

#ifndef BARE_METAL

#ifndef __THROW
#define __THROW
#endif
#ifndef __nonnull
#define __nonnull(params)
#endif
#ifndef __wur
#define __wur
#endif
#include_next <termios.h>

#else

typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[32];
};

#ifndef ICANON
#define ICANON 0x0001
#define ECHO 0x0002
#define TCSANOW 0
#endif

// Standard file descriptors
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

#endif
