/**
 * @file freertos_pci.c
 * @brief A FreeRTOS-based PCI task implementation.
 */
#ifdef ENABLE_PCI
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "event.h"
#include "log.h"
#include "log_marker.h"
#include "pci.h"
#include "utils.h"

#define PCI_SUSPENDED BIT(1)

// prototypes without include file
void test_pci(void);

// forward declarations
void vPCIControlTask(void *pvParameters);

// -- Globals --

extern uint32_t suspended;

// PCI Task resources
#define PCI_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
alignas(8) static StackType_t pciStack[PCI_STACK_SIZE];
alignas(8) static StaticTask_t pciTcb;
alignas(8) TaskHandle_t xPciHandle = nullptr;

// -- End of globals --

// Create the PCI Task
int create_pci_task(void)
{
    xPciHandle = xTaskCreateStatic(
        vPCIControlTask, "PciCtrlTask", PCI_STACK_SIZE, nullptr,
        tskIDLE_PRIORITY + 2, //  priority Medium
        pciStack, &pciTcb
    );
    if (xPciHandle == nullptr) {
        printf("\r[RTOS] Failed to create PCI task\n");
        return -1;
    }
    return 0;
}

void pci_start(void)
{
    if (suspended & PCI_SUSPENDED) {
        LOG_PCI_INFO("PCI Control Starting...");
        vTaskResume(xPciHandle);
        suspended &= ~PCI_SUSPENDED;
    }
}

void pci_suspend(void)
{
    if (!(suspended & PCI_SUSPENDED)) {
        LOG_PCI_INFO("PCI Control Ending (Entering Dormant State)...");
        vTaskSuspend(xPciHandle);
        suspended |= PCI_SUSPENDED;
    }
}

void pci_test_start(void)
{
    if (xPciHandle != nullptr) {
        xTaskNotify(xPciHandle, EVT_PCI_TEST, eSetBits);
    }
}

void vPCIControlTask(void *pvParameters)
{
    (void)pvParameters;
    printf("\r[RTOS] PCI Control Suspended\r\n");
    suspended |= PCI_SUSPENDED;
    vTaskSuspend(nullptr);
    LOG_PCI_INFO("PCI Control Awaken");

    uint32_t event_notify = 0;

    for (;;) {
        if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &event_notify, portMAX_DELAY) == pdTRUE) {
            if (event_notify & EVT_PCI_TEST) {
                test_pci();
            }
        }
    }
    vTaskDelete(nullptr);
}

#else // !PCI

int create_pci_task(void)
{
    return 0;
}

#endif // ENABLE_PCI