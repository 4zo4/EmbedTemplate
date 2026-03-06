#ifdef BARE_METAL
#include <stdint.h>
#include <termios.h>

int tcgetattr(int fd, struct termios *termios_p)
{
    if (!termios_p)
        return -1;
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    if (!termios_p)
        return -1;

    // Bridge logic: If the CLI turns off ECHO, tell the HW/Driver
    bool echo_enabled = (termios_p->c_lflag & ECHO) != 0;
    // uart_set_echo(echo_enabled);

    // If the CLI turns off ICANON, the UART driver remains in char-by-char mode
    bool canon_enabled = (termios_p->c_lflag & ICANON) != 0;
    // uart_set_buffered_mode(canon_enabled);

    return 0;
}

#endif