/**
 * @file freertos_main.c
 * @brief A FreeRTOS-based main function for the RTOS build.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "arch_ops.h"

// prototypes without include file

// CLI Interface (defined in cli_main.c)
int  cli_init(void **cli_ctx);
bool cli_run(void *cli_ctx);
void cli_exit(void *cli_ctx);

int create_pci_task(void);
int create_sim_tasks(void);

int init_hw(void);

uint64_t get_timestamp48(void);

// -- Globals --

extern volatile bool keep_running;

// Cli Task resources
#define CLI_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
alignas(8) static StackType_t cliStack[CLI_STACK_SIZE];
alignas(8) static StaticTask_t cliTcb;
alignas(8) TaskHandle_t xCliHandle = nullptr;

// Idle Task resources
alignas(8) static StaticTask_t idleTcb;
alignas(8) static StackType_t idleStack[configMINIMAL_STACK_SIZE];

// Timer Task resources
alignas(8) static StaticTask_t timerTcb;
alignas(8) static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];

alignas(8) uint32_t suspended; // bitmap of suspended system entities

// -- End of globals --

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, StackType_t *pulIdleTaskStackSize
)
{
    *ppxIdleTaskTCBBuffer = &idleTcb;
    *ppxIdleTaskStackBuffer = idleStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, StackType_t *pulTimerTaskStackSize
)
{
    *ppxTimerTaskTCBBuffer = &timerTcb;
    *ppxTimerTaskStackBuffer = timerStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

// FreeRTOS Idle Hook to allow the host CPU to rest when idle
void vApplicationIdleHook(void)
{
#ifdef BARE_METAL
    HALT_CPU();
#else
    /*
     * Force the Posix thread to sleep for a short duration.
     * 1000 microseconds = 1ms
     */
    usleep(1000);
#endif
}

void vCLITask(void *pvParameters)
{
    void *cli_ctx = nullptr;
    (void)pvParameters;

    if (cli_init(&cli_ctx) != 0) {
        printf("\r[RTOS] Failed to initialize CLI context\n");
        vTaskDelete(nullptr);
        return;
    }

    while (keep_running) {
        /*
         * We must yield control to the FreeRTOS scheduler to allow
         * other tasks (and the Idle task) to run.
         */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        keep_running = cli_run(cli_ctx);
    }

    cli_exit(cli_ctx);
    vTaskDelete(nullptr);
}

int main(void)
{
    init_hw();         // initialize hardware
    get_timestamp48(); // start time

    // Create the CLI Task
    xCliHandle = xTaskCreateStatic(
        vCLITask, "CLITask", CLI_STACK_SIZE, nullptr,
        tskIDLE_PRIORITY + 1, // Low priority
        cliStack, &cliTcb
    );
    if (xCliHandle == nullptr) {
        printf("\r[RTOS] Failed to create CLI task\n");
        return -1;
    }

    // create system entities tasks
    create_sim_tasks();
    create_pci_task();

#ifndef BARE_METAL
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGIO);                  // Unblock SIGIO for interrupt handling
    sigaddset(&set, SIGTERM);                // Unblock SIGTERM for graceful shutdown
    sigprocmask(SIG_UNBLOCK, &set, nullptr); // Unblock in main thread (POSIX)
    // Unblock in all threads (POSIX)
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
#endif
    printf("\r[RTOS] Starting Scheduler...\n");
    /*
     * Start the scheduler.
     * In the POSIX port, this will take over the main thread.
     */
    vTaskStartScheduler();

    // Should never reach here
    for (;;)
        ;
    return 0;
}
