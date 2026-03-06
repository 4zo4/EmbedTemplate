#include <signal.h>

#ifdef BARE_METAL
sighandler_t signal(int signum, sighandler_t handler)
{
    (void)signum;
    (void)handler;
    return (sighandler_t)0;
}
#endif