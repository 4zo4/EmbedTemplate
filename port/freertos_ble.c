/**
 * @file freertos_ble.c
 * @brief A FreeRTOS-based BLE task implementation.
 */
#ifdef ENABLE_BLE
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "event.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

#define BLE_SUSPENDED BIT(2)

#define LOG_BLE_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_BLE), __VA_ARGS__)

// forward declarations
void vBleHostTask(void *pvParameters);

void nimble_port_freertos_init(TaskFunction_t host_task_fn);
void nimble_port_run(void);

// -- Globals --

extern uint32_t suspended;

// BLE Task resources
#define BLE_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
alignas(8) static StackType_t bleHostStack[BLE_STACK_SIZE];
alignas(8) static StaticTask_t bleHostTcb;
alignas(8) TaskHandle_t xBleHostHandle = nullptr;

// -- End of globals --

// Create the BLE Task
int create_ble_task(void)
{
    xBleHostHandle = xTaskCreateStatic(
        vBleHostTask, "BleHostTask", BLE_STACK_SIZE, nullptr,
        tskIDLE_PRIORITY + 3, //  priority Medium
        bleHostStack, &bleHostTcb
    );
    if (xBleHostHandle == nullptr) {
        printf("\r[RTOS] Failed to create BLE Host task\n");
        return -1;
    }
    log_set_level(DOMAIN_SYS, ENTITY_BLE, LOG_LEVEL_INFO);
    return 0;
}

void ble_start(void)
{
    if (suspended & BLE_SUSPENDED) {
        LOG_BLE_INFO("BLE Host Starting...");
        vTaskResume(xBleHostHandle);
        suspended &= ~BLE_SUSPENDED;
    }
}

void ble_suspend(void)
{
    if (!(suspended & BLE_SUSPENDED)) {
        LOG_BLE_INFO("BLE Host Ending (Entering Dormant State)...");
        vTaskSuspend(xBleHostHandle);
        suspended |= BLE_SUSPENDED;
    }
}

void vBleHostTask(void *pvParameters)
{
    (void)pvParameters;
    bool once = true;

    printf("\r[RTOS] BLE Host Suspended\r\n");
    suspended |= BLE_SUSPENDED;
    vTaskSuspend(nullptr);
    LOG_BLE_INFO("BLE Host Awaken");

    if (once) {
        nimble_port_freertos_init(nullptr);
        once = false;
    }
    nimble_port_run();

    vTaskDelete(nullptr);
}

#else // !BLE

int create_ble_task(void)
{
    return 0;
}

#endif // ENABLE_BLE