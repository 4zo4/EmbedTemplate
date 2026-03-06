#pragma once

typedef void (*sighandler_t)(int);

#define SIGINT 2
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGTERM 15
#define SIGIO 29

void         signal_handler(int sig);
sighandler_t signal(int signum, sighandler_t handler);