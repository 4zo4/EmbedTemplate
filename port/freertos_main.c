/**
 * @file rtos_main.c
 * @brief An RTOS-based test for a simulated GPIO-controlled thermal
 * system. This example demonstrates a realistic temperature regulation
 * scenario with a heater, cooling fans, and an over-temperature alarm using
 * FreeRTOS tasks.
 *
 * The system simulates a temperature sensor that can read from -40°C to
 * +215°C, with a target temperature of 125°C. The heater and fans are
 * controlled via GPIO output pins, while the temperature sensor is read from
 * GPIO input pins. An over-temperature alarm is triggered if the temperature
 * exceeds the target by a certain margin.
 *
 * The hardware SoC simulation task models the thermal dynamics of the
 * system, including active heating, cooling, thermal runaway effects, and
 * passive dissipation. The control task implements a regulation algorithm with
 * hysteresis to maintain stable temperature control.
 *
 * @note This code is designed for educational purposes to demonstrate
 * temperature regulation logic, FreeRTOS task management, and FW access to HW
 * concepts.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "gpio_regs.h"
#include "gpio.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

// Logging Macros for Simulation specific System-Level Messages
#define LOG_SYS_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_SIM), __VA_ARGS__)
#define LOG_SYS_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_SIM), __VA_ARGS__)
#define LOG_SYS_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_SIM), __VA_ARGS__)
#define LOG_SYS_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_SIM), __VA_ARGS__)
#define LOG_SYS_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_SIM), __VA_ARGS__)

// Shift the temperature range to fit into 0-255 tange (e.g., -40°C
// becomes 0, +215°C becomes 255)
#define SENSOR_OFFSET 40
#define TEMP_TARGET 125   // Target temperature 125°C
#define TEMP_CRITICAL 180 // Critical temperature 180°C
// Target 50°C for cooldown before allowing heater to turn on again
#define COOLDOWN_TARGET 50
#define TEMP_HYSTERESIS 10 // Prevent rapid flapping

// Constants for Chip Physics
#define AMBIENT_TEMP_MC 25000   // 25°C
#define CHIP_THERMAL_MASS 150   // Higher = slower temp changes
#define LEAKAGE_THRESHOLD 85000 // Silicon leakage starts dominating > 85°C
#define BOARD_CONDUCTANCE 20    // Higher = poor heat dissipation
#define RUNAWAY_COEFF 4         // Higher = weaker runaway effect

// prototypes without include file
// Mock alarm interrupt and periodic temperature update signal from the hardware simulator to the temperature regulation
// vGPIOControlTask.
void init_mock_interrupts(void);
// CLI Interface (defined in cli_main.c)
int  cli_init(void **cli_ctx);
bool cli_run(void *cli_ctx);
void cli_exit(void *cli_ctx);
// Temperature Regulation Logic
void     run_temperature_regulation(volatile gpio_ctrl_t *gpio);
uint64_t get_timestamp48(void);

// -- Globals --

extern bool              keep_running;
extern SemaphoreHandle_t xInterruptSem;

#define NORMAL 0
#define PAUSE 1
#define PAUSE_ACK 2

static int  system_mode = NORMAL; // 0 = Normal, 1 = Pause, 2 = Ack the pause
static bool suspended = true;

// Cli Task resources
#define CLI_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)
static StackType_t  cliStack[CLI_STACK_SIZE];
static StaticTask_t cliTcb;
static TaskHandle_t xCliHandle = nullptr;
// Ctrl Task resources
#define CTRL_STACK_SIZE (configMINIMAL_STACK_SIZE * 2) // 2x stack for control logic
static StackType_t  ctrlStack[CTRL_STACK_SIZE];
static StaticTask_t ctrlTcb;
static TaskHandle_t xCtrlHandle = nullptr;
// HW Task resources
#define HW_STACK_SIZE (configMINIMAL_STACK_SIZE * 2) // 2x stack for simulation
static StackType_t  hwStack[HW_STACK_SIZE];
static StaticTask_t hwTcb;
static TaskHandle_t xHwHandle = nullptr;
// Idle Task resources
static StaticTask_t idleTcb;
static StackType_t  idleStack[configMINIMAL_STACK_SIZE];
// Timer Task resources
static StaticTask_t timerTcb;
static StackType_t  timerStack[configTIMER_TASK_STACK_DEPTH];

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
    /*
     * Force the Posix thread to sleep for a short duration.
     * 1000 microseconds = 1ms
     */
    usleep(1000);
}

bool is_suspended(void)
{
    return suspended;
}

void sim_start(void)
{
    LOG_SYS_INFO("Simulation Starting...");
    vTaskResume(xCtrlHandle);
    vTaskResume(xHwHandle);
    suspended = false;
}

void sim_suspend(void)
{
    LOG_SYS_INFO("Simulation Ending (Entering Dormant State)...");
    vTaskSuspend(xHwHandle);
    vTaskSuspend(xCtrlHandle);
    suspended = true;
}

bool sim_pause(void)
{
    if (suspended)
        return true;

    // Pause hardware simulation
    // Sleep until the hardware simulator acknowledges the mode change
    system_mode = PAUSE;
    while (system_mode != PAUSE_ACK) {
        usleep(1000);
    }
    return true;
}

void sim_resume(void)
{
    // Resume hardware simulation
    if (system_mode == PAUSE_ACK) {
        system_mode = NORMAL;
    }
}

void vCLITask(void *pvParameters)
{
    void *cli_ctx = nullptr;
    (void)pvParameters;

    if (cli_init(&cli_ctx) != 0) {
        printf("[RTOS] Failed to initialize CLI context\n");
        vTaskDelete(nullptr);
        return;
    }

    while (keep_running) {
        keep_running = cli_run(cli_ctx);
        /*
         * We must yield control to the FreeRTOS scheduler to allow
         * other tasks (and the Idle task) to run.
         */
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    cli_exit(cli_ctx);
    vTaskDelete(nullptr);
}

/**
 * @brief The Hardware Simulator
 */
void vHardwareSimTask(void *pvParameters)
{
    volatile gpio_ctrl_t *gpio = (volatile gpio_ctrl_t *)pvParameters;
    int32_t               sim_temp_mC = AMBIENT_TEMP_MC; // 25.000 °C initial state
    uint32_t              ms_counter = 0;
    bool                  resume = false;

    printf("[RTOS] HW Simulation Suspended.\n");
    vTaskSuspend(NULL);
    LOG_SYS_INFO("HW Simulation Awaken.");

    for (;;) {
        if (system_mode == PAUSE) {
            system_mode = PAUSE_ACK;
            resume = true;
            LOG_SYS_INFO("HW Simulation Paused. System in Test Mode.");
        }
        if (system_mode == PAUSE_ACK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (resume) {
            resume = false;
            LOG_SYS_INFO("HW Simulation Resumed. System in Normal Mode.");
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms simulation tick

        int32_t net_heat_power = 0;
        int32_t cooling_power = 0;
        bool    done = false;

        ms_counter++;

        // - PHYSICS: Update internal state -

        // --- ACTIVE HEATING: Heater Current (mA) ---
        if (gpio->out.f.pin_out & 0x01) {
            // Heater is active, adds heat power (e.g., +4°C per ms)
            net_heat_power += 4000;
        }

        // --- THERMAL RUNAWAY ---
        // Leakage power increases exponentially/linearly as temp rises
        if (sim_temp_mC > LEAKAGE_THRESHOLD) {
            // Power dissipation increases based on current temp (Positive
            // Feedback)
            net_heat_power += (sim_temp_mC - LEAKAGE_THRESHOLD) / RUNAWAY_COEFF;
        }

        // --- COOLING & DISSIPATION: Board Conductance ---
        if (gpio->out.f.pin_out & 0x02) {
            cooling_power += 5000; // Fan 1 is active, strong cooling power
                                   // (e.g., -5°C per ms)
        }
        if (gpio->out.f.pin_out & 0x04) {
            cooling_power += 45000; // Fan 2 is active, emergency cooling power
                                    // (e.g., -45°C per ms)
        }
        // PASSIVE COOLING: Dissipation to ambient based on board quality
        // The hotter it is, the more it dissipates (Standard Newtonian Cooling)
        cooling_power += (sim_temp_mC - AMBIENT_TEMP_MC) / BOARD_CONDUCTANCE;

        // Apply net change to internal state
        sim_temp_mC += (net_heat_power - cooling_power) / CHIP_THERMAL_MASS;

        // Hard Physical Limits of the Sensor (-40°C to 215°C)
        if (sim_temp_mC > 215000)
            sim_temp_mC = 215000;
        if (sim_temp_mC < -40000)
            sim_temp_mC = -40000;

        // Convert millidegrees to 8-bit integer
        int32_t temp = (sim_temp_mC / 1000) + SENSOR_OFFSET;

        if (temp > 255)
            temp = 255; // Clamp at Max (215°C)
        if (temp < 0)
            temp = 0; // Clamp at Min (-40°C)

        // Update the sensor reading in the input register
        gpio->in.f.pin_in = (uint8_t)temp;

        // --- ALARM ---
        // If temperature exceeds target + hysteresis + 10°C buffer then
        // trigger the alarm and hold its irq until reset by the user
        if (sim_temp_mC >= (1000 * ((TEMP_TARGET + TEMP_HYSTERESIS + 10)))) {
            bool is_enabled = (gpio->int_en.f.en & (1 << 8));
            bool is_pending = (gpio->int_sts.f.status & (1 << 8));

            if (is_enabled && !is_pending) {
                gpio->in.f.alarm = 1;               // The Over-Temp Alarm bit is set by the real HW
                                                    // when the temperature exceeds the threshold
                gpio->int_sts.f.status |= (1 << 8); // Latch the alarm interrupt
                done = true;                        // signal only once (see below for the normal
                                                    // sensor update)
                // Delay next sensor update (below) to 30ms after alarm trigger
                // to allow temperature to drop a bit (see
                // run_temperature_regulation)
                if (ms_counter > 15)
                    ms_counter = 0;
                // Trigger the Alarm IRQ via Unix Signal (SIGIO)
                kill(getpid(), SIGIO);
                LOG_SYS_INFO("Over-Temp Alarm Triggered at %d°C", (sim_temp_mC / 1000));
            }
        } else {
            // Temperature dropped below threshold: HW clears the input pin
            // alarm
            gpio->in.f.alarm = 0;
        }
        // --- SENSOR: Update Input Register every 30ms ---
        if (ms_counter >= 30 && !done) {
            ms_counter = 0;
            kill(getpid(), SIGIO); // Yield via Unix Signal (SIGIO)
        }
        // --- WATCHDOG ---
        if (gpio->wdt_cfg.f.en) {
            if (gpio->wdt_val.f.timer > 0) {
                gpio->wdt_val.f.timer--;
            } else {
                // Shutdown heater, Fan 1 ON (Fan 2 is reserved for critical
                // runaway scenarios)
                gpio->out.f.pin_out = 0x02;
                LOG_SYS_CRITICAL("HW Watchdog Triggered! Forcing Safety Mode.");
            }
        }
    }
    vTaskDelete(nullptr);
}

void vGPIOControlTask(void *pvParameters)
{
    volatile gpio_ctrl_t *gpio = (volatile gpio_ctrl_t *)pvParameters;

    printf("[RTOS] Temperature Control Suspended.\n");
    vTaskSuspend(NULL);
    LOG_SYS_INFO("Temperature Control Awaken.");

    // Set log levels for simulation and gpio
    log_set_level(DOMAIN_SYS, ENTITY_SIM, LOG_LEVEL_INFO);
    log_set_level(DOMAIN_DEV, ENTITY_GPIO, LOG_LEVEL_INFO);

    // Initialize the GPIO Controller to a known safe state
    gpio_init_controller(gpio);

    // Configure Pin 0: Heater (Output), Pin 1: Cooler (Output),
    //           Pin 2: Emergency Cooler (Output),
    //           Pin 0-7: Sensor Inputs (Temperature), Pin 8: Alarm Input
    gpio_init_pin(gpio, 0, true);
    gpio_init_pin(gpio, 1, true);
    gpio_init_pin(gpio, 2, true);
    gpio_init_pin(gpio, 8, false);
    gpio_init_pin_mask(gpio, 0xFF, false);

    // Configure and Enable Interrupt for the Alarm Pin
    gpio_configure_interrupt(gpio, 8, false); // Active High
    // Clear any stale status from the init/boot period
    gpio_clear_interrupt(gpio, 8);
    // Configure Watchdog with a 150ms timeout
    gpio_wdt_setup(gpio, 150);

    for (;;) {
        if (xSemaphoreTake(xInterruptSem, portMAX_DELAY) == pdPASS) {
            run_temperature_regulation(gpio);
            gpio_wdt_kick(gpio); // Reset the Watchdog timer on each regulation cycle
        }
    }
    vTaskDelete(nullptr);
}

int main(void)
{
    alignas(8) static volatile gpio_ctrl_t gpio; // Local instance of GPIO registers -
                                                 // simulated memory-mapped hardware
    get_timestamp48();                           // start time
    gpio_set_regs(&gpio);                        // Set the base address for the GPIO driver

    // Create the binary semaphore for interrupt synchronization
    init_mock_interrupts();

    // Create the CLI Task
    xCliHandle = xTaskCreateStatic(
        vCLITask, "CLITask", CLI_STACK_SIZE, nullptr,
        tskIDLE_PRIORITY + 1, // Low priority
        cliStack, &cliTcb
    );
    if (xCliHandle == nullptr) {
        printf("[RTOS] Failed to create CLI task\n");
        return -1;
    }

    // Create GPIO Controller Task (Priority: Medium)
    xCtrlHandle = xTaskCreateStatic(
        vGPIOControlTask, "GpioCtrlTask", CTRL_STACK_SIZE, (void *)&gpio,
        tskIDLE_PRIORITY + 2, // Higher than CLI but lower than HwSimTask
        ctrlStack, &ctrlTcb
    );
    if (xCtrlHandle == nullptr) {
        printf("[RTOS] Failed to create GPIO Controller task\n");
        return -1;
    }
    // Create Hardware Simulator Task (Priority: High)
    xHwHandle = xTaskCreateStatic(
        vHardwareSimTask, "HwSimTask", HW_STACK_SIZE, (void *)&gpio,
        configMAX_PRIORITIES - 1, // High priority to ensure timely simulation
        hwStack, &hwTcb
    );
    if (xHwHandle == nullptr) {
        printf("[RTOS] Failed to create Hardware Simulator task\n");
        return -1;
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGIO);                  // Unblock SIGIO for interrupt handling
    sigaddset(&set, SIGTERM);                // Unblock SIGTERM for graceful shutdown
    sigprocmask(SIG_UNBLOCK, &set, nullptr); // Unblock in main thread (POSIX)
    // Unblock in all threads (POSIX)
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

    /*
     * Start the scheduler.
     * In the POSIX port, this will take over the main thread.
     */
    printf("[RTOS] Starting Scheduler...\n");

    vTaskStartScheduler();

    // Should never reach here
    for (;;)
        ;
    return 0;
}

// -- Globals --

static int16_t  max_temp = -SENSOR_OFFSET;
static uint32_t time_window = 0;
static bool     alarm_triggered = false;
static bool     is_heating = false;
static bool     is_cooldown = false;
static bool     is_critical = false;

// -- End of globals --

void run_temperature_regulation(volatile gpio_ctrl_t *gpio)
{
    uint8_t raw = gpio_get_all_in_pins(gpio);
    int16_t temp = (int16_t)raw - SENSOR_OFFSET;

    if (temp > max_temp) {
        max_temp = temp;
    }
    if ((time_window++ & (128 - 1)) == 0) { // Every 128 x 30ms, log the current temperature
        LOG_SYS_INFO("Temperature %d°C (%u) Max Temperature %d°C", temp, raw, max_temp);
    }

    // If temperature exceeds critical threshold, turn on emergency cooling (Fan 2)
    // regardless of alarm state
    if (temp > TEMP_CRITICAL) {
        is_critical = true;
        gpio_set_out_pin(gpio, 2); // Fan 2 ON (Emergency Cooling)
    }
    if (gpio_is_alarm(gpio) && !alarm_triggered) {
        is_heating = false;
        is_cooldown = true;
        gpio_clear_out_pin(gpio, 0); // Heater OFF
        gpio_set_out_pin(gpio, 1);   // Fan 1 ON

        alarm_triggered = true;
        LOG_SYS_WARNING("Over-Temp Alarm! Temperature %d°C Forcing Safety Cooling", temp);
        return; // An early exit
    }
    if (alarm_triggered && temp < TEMP_TARGET) {
        alarm_triggered = false;
        // Acknowledge the alarm, i.e. clear its interrupt status
        gpio_clear_interrupt(gpio, 8);
        // clang-format off
        LOG_SYS_INFO("Temperature %d°C dropped below the threshold " STR(TEMP_TARGET) "°C, "
                     "starting cooldown", temp);
        // clang-format on
    }

    if (is_cooldown) {
        if (temp > COOLDOWN_TARGET) {
            return; // not cool enough yet, keep cooling loop active and skip
                    // the rest of the regulation logic
        } else {
            is_cooldown = false;
            // Cooldown complete, can resume normal operation. If the initial
            // temperature was above critical shutdown the emergency fan
            if (is_critical) {
                is_critical = false;
                gpio_clear_out_pin(gpio, 2); // Fan 2 OFF
            }
            // clang-format off
            LOG_SYS_INFO("Temperature %d°C dropped below the threshold " STR(COOLDOWN_TARGET) "°C, "
                         "resuming normal operation", temp);
            // clang-format on
        }
    }

    // Normal regulation logic with hysteresis to prevent flapping
    if (temp < (TEMP_TARGET - TEMP_HYSTERESIS)) {
        is_heating = true;
    } else if (temp > TEMP_TARGET) {
        is_heating = false;
    }

    if (is_heating) {
        gpio_set_out_pin(gpio, 0);   // Heater ON
        gpio_clear_out_pin(gpio, 1); // Fan 1 OFF
    } else {
        if (temp > (TEMP_TARGET + 3)) { // Small deadband to prevent fan jitter
            gpio_set_out_pin(gpio, 1);  // Fan 1 ON
        } else {
            gpio_clear_out_pin(gpio, 1); // Fan 1 OFF
        }
        gpio_clear_out_pin(gpio, 0); // Heater OFF
    }
}
