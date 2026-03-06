#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "term_codes.h"

static StaticSemaphore_t xInterruptSemInst;
SemaphoreHandle_t        xInterruptSem = nullptr;

void vMockISR_Handler(int sig)
{
#ifdef ENABLE_DEBUG
    char buf[64];
    // clang-format off
    int  len = snprintf(buf, sizeof(buf), "\r\n" UI_COLOR_YELLOW "[SIGNAL] "
                        "Received %d." UI_STYLE_RESET "\r\n", sig);
    // clang-format on
    write(STDOUT_FILENO, buf, len);
#else
    (void)sig;
#endif
    if (xInterruptSem != nullptr) {
        // DO NOT call portYIELD_FROM_ISR in the POSIX port.
        // Just give the semaphore. The scheduler will see it on the next tick.
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xInterruptSem, &xHigherPriorityTaskWoken);
    }
}

void vInterruptHandlerTask(void *pvParameters)
{
    (void)pvParameters;
    const char *msg = "[Task] Interrupt Task unblocked!\n";
    for (;;) {
        if (xSemaphoreTake(xInterruptSem, portMAX_DELAY) == pdPASS) {
            write(STDOUT_FILENO, msg, strlen(msg));
        }
    }
}

void init_mock_interrupts(void)
{
    if (xInterruptSem == nullptr)
        xInterruptSem = xSemaphoreCreateBinaryStatic(&xInterruptSemInst);

    struct sigaction sa = {0};
    sa.sa_handler = vMockISR_Handler;
    sigemptyset(&sa.sa_mask);
    // Prevent syscalls like poll() from
    // failing when signal arrives
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGIO, &sa, nullptr) == -1) {
        perror("sigaction SIGIO");
    }
}