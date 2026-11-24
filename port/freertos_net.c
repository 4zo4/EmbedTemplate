/**
 * @file freertos_ble.c
 * @brief A FreeRTOS-based BLE task implementation.
 */
#ifdef ENABLE_NET
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
#define MQTT_SUSPENDED BIT(3)

#define LOG_BLE_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_BLE), __VA_ARGS__)
#define LOG_MQTT_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_MQTT), __VA_ARGS__)

// forward declarations
void vBleHostTask(void *pvParameters);
void vMqttTask(void *pvParameters);

void nimble_port_run(void);
void nimble_host_init(void);
int  mqtt_init(void);
void mqtt_run(void);
int  mqtt_ble_transport_init(void);
int  mqtt_ble_transport_cfg(void);

// -- Globals --

extern uint32_t suspended;

// BLE Task resources
#define BLE_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
alignas(8) static StackType_t bleHostStack[BLE_STACK_SIZE];
alignas(8) static StaticTask_t bleHostTcb;
alignas(8) TaskHandle_t xBleHostHandle = nullptr;

// MQTT Task resources
#define MQTT_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
alignas(8) static StackType_t mqttStack[MQTT_STACK_SIZE];
alignas(8) static StaticTask_t mqttTcb;
alignas(8) TaskHandle_t xMqttHandle = nullptr;

// -- End of globals --

// Create the BLE Task
int create_net_tasks(void)
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
    xMqttHandle = xTaskCreateStatic(
        vMqttTask, "MqttTask", MQTT_STACK_SIZE, nullptr,
        tskIDLE_PRIORITY + 3, //  priority Medium
        mqttStack, &mqttTcb
    );
    if (xMqttHandle == nullptr) {
        printf("\r[RTOS] Failed to create MQTT task\n");
        return -1;
    }
    log_set_level(DOMAIN_SYS, ENTITY_BLE, LOG_LEVEL_INFO);
    log_set_level(DOMAIN_SYS, ENTITY_MQTT, LOG_LEVEL_INFO);
    return 0;
}

void ble_start(void)
{
    if (suspended & BLE_SUSPENDED) {
        LOG_BLE_INFO("BLE Host Starting...");
        vTaskResume(xBleHostHandle);
        suspended &= ~BLE_SUSPENDED;
        vTaskResume(xMqttHandle);
        suspended &= ~MQTT_SUSPENDED;
    }
}

void ble_suspend(void)
{
    if (!(suspended & BLE_SUSPENDED)) {
        LOG_BLE_INFO("BLE Host Ending (Entering Dormant State)...");
        vTaskSuspend(xBleHostHandle);
        suspended |= BLE_SUSPENDED;
        vTaskSuspend(xMqttHandle);
        suspended |= MQTT_SUSPENDED;
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
        nimble_host_init();
        mqtt_ble_transport_init();
        mqtt_ble_transport_cfg();
        once = false;
    }
    nimble_port_run();

    vTaskDelete(nullptr);
}

void vMqttTask(void *pvParameters)
{
    (void)pvParameters;
    bool once = true;

    printf("\r[RTOS] MQTT Suspended\r\n");
    suspended |= MQTT_SUSPENDED;
    vTaskSuspend(nullptr);
    LOG_MQTT_INFO("MQTT Awaken");

    if (once) {
        mqtt_init();
        once = false;
    }
    mqtt_run();

    vTaskDelete(nullptr);
}

#else // !NET

int create_net_tasks(void)
{
    return 0;
}

#endif // ENABLE_NET
