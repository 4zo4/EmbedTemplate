#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "arch_ops.h"

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    printf("\r\n%s:%d: %s: Assertion `%s' failed.\r\n", file, line, func, expr);
    printf("Aborted\r\n");
    printf("[HALT] System locked. Waiting for power-on reset (POR)...\r\n");
    while (true) {
        HALT_CPU();
    }
}