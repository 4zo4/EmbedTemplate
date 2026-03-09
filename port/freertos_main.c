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

#include "gpio_demo_regs.h"
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
#define SENSOR_OFFSET 40  // -40°C
#define TEMP_TARGET 125   // Target temperature 125°C
#define TEMP_CRITICAL 180 // Critical temperature 180°C
// Target 50°C for cooldown before allowing heater to turn on again
#define TEMP_COOLDOWN 50
#define TEMP_HYSTERESIS 10        // Prevent rapid flapping
#define RAMP_TIME_DEFAULT 10      // 10 sec
#define SENSOR_LATENCY_DEFAULT 20 // 20 sec

// Constants for Chip Physics
#define AMBIENT_TEMP_MC 25000   // 25°C
#define CHIP_THERMAL_MASS 400   // Higher = slower temp changes
#define LEAKAGE_THRESHOLD 85000 // Silicon leakage starts dominating > 85°C
#define BOARD_CONDUCTANCE 8192  // Higher = poor heat dissipation
#define RUNAWAY_COEFF 4         // Higher = weaker runaway effect
#define HEATER_UNIT_POWER 4000  // Heater power

// --- Simulation Physics ---

typedef struct phy_state_s {
    uint32_t ramp_time_s;       // Range: 2 to 20 sec
    uint32_t thermal_mass;      // Range: 200 - 1000 (200 = Fast (2s), 1000 = Very Slow (25s))
    uint32_t board_conduct;     // Range: 5000 - 15000 (Higher = Holds heat for minutes)
    uint32_t heater_power;      // Range: 1000 - 8000
    uint32_t sensor_latency_ms; // Range: 5-50 (ms)
} phy_state_t;

typedef struct sim_state_s {
    int32_t energy_balance;
    int32_t net_heat_power;
    int32_t cooling_power;
} sim_state_t;

typedef union ctrl_status_u {
    struct ctrl_status_s {
        uint32_t alarm_triggered : 1;
        uint32_t is_heating : 1;
        uint32_t is_cooldown : 1;
        uint32_t is_critical : 1;
    } bits;
    uint32_t all;
} ctrl_status_t;

typedef struct temp_ctrl_s {
    ctrl_status_t status;
    uint32_t      time_window;
    int16_t       max_temp;
} temp_ctrl_t;

typedef union unit_status_u {
    struct unit_status_s {
        uint32_t is_heater_failed : 1;
        uint32_t is_fan1_failed : 1;
        uint32_t is_fan2_failed : 1;
    } bits;
    uint32_t all;
} unit_status_t;

typedef struct sim_ctx_s {
    phy_state_t   phy;     // The Chip Physics (Heater, Mass, Latency)
    temp_ctrl_t   reg;     // The Temp Controller Regulation State
    sim_state_t   state;   // The Runtime state (Energy Balance)
    unit_status_t status;  // Unit status
    int32_t       temp_mC; // Current temperature
} sim_ctx_t;

typedef struct temp_ctrl_cfg_s {
    uint16_t temp_target;
    uint16_t temp_critical;
    uint16_t temp_hysteresis;
    uint16_t temp_cooldown;
} temp_ctrl_cfg_t;

// prototypes without include file
// Mock alarm interrupt and periodic temperature update signal from the hardware simulator to the temperature regulation
// vGPIOControlTask.
void init_mock_interrupts(void);
// CLI Interface (defined in cli_main.c)
int      cli_init(void **cli_ctx);
bool     cli_run(void *cli_ctx);
void     cli_exit(void *cli_ctx);
uint64_t get_timestamp48(void);

static void reset_sim_state(bool phy);

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
// Simulation data
static sim_ctx_t       sim_ctx;
static temp_ctrl_cfg_t sim_cfg;

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
    reset_sim_state(false);
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
    if (system_mode == PAUSE_ACK)
        system_mode = NORMAL;
}

static void temp_ctrl_cfg_init(void)
{
    sim_cfg = (temp_ctrl_cfg_t){
        .temp_target = TEMP_TARGET,
        .temp_critical = TEMP_CRITICAL,
        .temp_hysteresis = TEMP_HYSTERESIS,
        .temp_cooldown = TEMP_COOLDOWN,
    };
}

static void reset_sim_state(bool phy)
{
    sim_ctx.temp_mC = AMBIENT_TEMP_MC;

    if (phy) {
        sim_ctx.phy = (phy_state_t){
            // Default ramp to reach the target temp
            .ramp_time_s = RAMP_TIME_DEFAULT,
            // Default provides ~10s ramp to 125C at 4000 power
            .thermal_mass = CHIP_THERMAL_MASS,
            // Default conductance provides slow passive heat bleed to board
            .board_conduct = BOARD_CONDUCTANCE,
            // Default heater strength
            .heater_power = HEATER_UNIT_POWER,
            // Default sensor latency
            .sensor_latency_ms = SENSOR_LATENCY_DEFAULT,
        };
    }
    sim_ctx.status = (unit_status_t){
        // clang-format off
        .bits = {
            .is_heater_failed = false,
            .is_fan1_failed = false,
            .is_fan2_failed = false,
        },
        // clang-format on
    };
    sim_ctx.reg = (temp_ctrl_t){
        .time_window = 0,
        .max_temp = -SENSOR_OFFSET,
        // clang-format off
        .status.bits = {
            .alarm_triggered = false,
            .is_heating = false,
            .is_cooldown = false,
            .is_critical = false,
        },
        // clang-format on
    };
}

void update_sim_phy(int target, int ramp, int power, int latency)
{
    if (target >= 40 || target <= 200)
        sim_cfg.temp_target = target;
    if (ramp >= 2 || ramp <= 20)
        sim_ctx.phy.ramp_time_s = ramp;
    if (power >= 500 || power <= 8000)
        sim_ctx.phy.heater_power = power;
    if (latency >= 1 || latency <= 100)
        sim_ctx.phy.sensor_latency_ms = latency;

    sim_ctx.phy.thermal_mass = (sim_ctx.phy.heater_power * sim_ctx.phy.ramp_time_s) / 100;

    if (sim_ctx.phy.thermal_mass < 1)
        sim_ctx.phy.thermal_mass = 1;

    sim_ctx.phy.board_conduct = HEATER_UNIT_POWER + (sim_ctx.phy.thermal_mass * 20);

    if (sim_ctx.phy.ramp_time_s < 4 && sim_ctx.phy.sensor_latency_ms > 10)
        sim_ctx.phy.sensor_latency_ms = 10;
}

static void run_temperature_regulation(volatile gpio_ctrl_t *gpio)
{
    uint8_t raw = gpio_get_all_in_pins(gpio);
    int16_t temp = (int16_t)raw - SENSOR_OFFSET;

    if (temp > sim_ctx.reg.max_temp)
        sim_ctx.reg.max_temp = temp;

    // Log every 128 * sensor_latency's ms
    if ((sim_ctx.reg.time_window++ & (128 - 1)) == 0)
        LOG_SYS_INFO("Temp: %d°C | Target: %u°C | Max: %d°C", temp, sim_cfg.temp_target, sim_ctx.reg.max_temp);

    // If temperature exceeds critical threshold, turn on emergency cooling (Fan 2)
    // regardless of alarm state
    if (temp > sim_cfg.temp_critical) {
        sim_ctx.reg.status.bits.is_critical = true;
        gpio_set_out_pin(gpio, 2);   // Fan 2 (Emergency) ON
        gpio_clear_out_pin(gpio, 0); // Force Heater OFF
    }

    if (gpio_is_alarm(gpio) && !sim_ctx.reg.status.bits.alarm_triggered) {
        sim_ctx.reg.status.bits.is_heating = false;
        sim_ctx.reg.status.bits.is_cooldown = true;
        gpio_clear_out_pin(gpio, 0); // Heater OFF
        gpio_set_out_pin(gpio, 1);   // Fan 1 (Normal) ON
        sim_ctx.reg.status.bits.alarm_triggered = true;
        LOG_SYS_WARNING("Over-Temp Alarm! Temperature %d°C Forcing Safety Cooling", temp);
        return; // An early exit
    }

    if (sim_ctx.reg.status.bits.alarm_triggered && (temp < sim_cfg.temp_target)) {
        sim_ctx.reg.status.bits.alarm_triggered = false;
        // Acknowledge the alarm, i.e. clear its interrupt status
        gpio_clear_interrupt(gpio, 8); // clear HW Latch
        // clang-format off
        LOG_SYS_INFO("Temperature %d°C dropped below the threshold %u°C, "
                     "starting cooldown", temp, sim_cfg.temp_target);
        // clang-format on
    }

    if (sim_ctx.reg.status.bits.is_cooldown) {
        if (temp >= sim_cfg.temp_cooldown) {
            if (!sim_ctx.status.bits.is_fan1_failed)
                gpio_set_out_pin(gpio, 1); // Ensure Fan 1 stays ON
            return;
        } else {
            sim_ctx.reg.status.bits.is_cooldown = false;
            if (sim_ctx.reg.status.bits.is_critical) {
                sim_ctx.reg.status.bits.is_critical = false;
                gpio_clear_out_pin(gpio, 2); // Fan 2 OFF
            }
            // clang-format off
            LOG_SYS_INFO("Temperature %d°C dropped below the threshold %u°C, "
                         "resuming normal operation", temp, sim_cfg.temp_cooldown);
            // clang-format on
        }
    }

    // Normal regulation logic with hysteresis to prevent flapping
    if (temp < (sim_cfg.temp_target - sim_cfg.temp_hysteresis)) {
        sim_ctx.reg.status.bits.is_heating = true;
    } else if (temp >= sim_cfg.temp_target) {
        sim_ctx.reg.status.bits.is_heating = false;
    }

    if (sim_ctx.reg.status.bits.is_heating) {
        gpio_set_out_pin(gpio, 0);   // Heater ON
        gpio_clear_out_pin(gpio, 1); // Fan 1 OFF
    } else {
        gpio_clear_out_pin(gpio, 0); // Heater OFF
        // Deadband: Only use Fan 1 if we overshot the target by 3 degrees
        if (temp > (sim_cfg.temp_target + 3)) {
            gpio_set_out_pin(gpio, 1);
        } else {
            gpio_clear_out_pin(gpio, 1);
        }
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
    uint32_t              ms_counter = 0;
    bool                  resume = false;

    printf("[RTOS] HW Simulation Suspended.\n");
    vTaskSuspend(NULL);

    LOG_SYS_INFO("HW Simulation Awaken.");
    temp_ctrl_cfg_init();
    reset_sim_state(true);

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

        sim_ctx.state = (sim_state_t){0};
        bool runaway = false;
        bool done = false;

        ms_counter++;

        // - PHYSICS: Update internal state -

        // Simulate Fan 1 failure
        if ((ms_counter == sim_ctx.phy.sensor_latency_ms) &&
            sim_ctx.reg.time_window && (sim_ctx.reg.time_window & (1024 - 1)) == 0)
            sim_ctx.status.bits.is_fan1_failed = !sim_ctx.status.bits.is_fan1_failed;

        if (sim_ctx.status.bits.is_fan1_failed)
            runaway = true;

        // ACTIVE HEATING: adjustable heater power
        if ((gpio->out.f.pin_out & 0x01 || runaway) && !sim_ctx.status.bits.is_heater_failed)
            sim_ctx.state.net_heat_power += (int32_t)sim_ctx.phy.heater_power;

        // ACTIVE COOLING: Fans balanced to heater power
        if (gpio->out.f.pin_out & 0x02 && !sim_ctx.status.bits.is_fan1_failed)
            sim_ctx.state.cooling_power += ((int32_t)sim_ctx.phy.heater_power * 3) / 2; // Fan 1 is 1.5 Heater
        if (gpio->out.f.pin_out & 0x04 && !sim_ctx.status.bits.is_fan2_failed)
            sim_ctx.state.cooling_power += (int32_t)sim_ctx.phy.heater_power * 4; // Fan 2 is 4x stronger

        // APPLY PHYSICS: Resulting delta is in millidegrees per millisecond
        sim_ctx.temp_mC += (sim_ctx.state.net_heat_power - sim_ctx.state.cooling_power) / (int32_t)sim_ctx.phy.thermal_mass;

        // Hard Physical Limits of the Sensor (-40°C to 215°C)
        if (sim_ctx.temp_mC > 215000)
            sim_ctx.temp_mC = 215000;
        if (sim_ctx.temp_mC < -40000)
            sim_ctx.temp_mC = -40000;

        // Convert millidegrees to 8-bit integer
        int32_t temp = (sim_ctx.temp_mC / 1000) + SENSOR_OFFSET;

        if (temp > 255)
            temp = 255; // Clamp at Max (215°C)
        if (temp < 0)
            temp = 0; // Clamp at Min (-40°C)

        // Update the sensor reading in the input register
        gpio->in.f.pin_in = (uint8_t)temp;

        // --- ALARM ---
        // If temperature exceeds target + hysteresis + 10°C buffer then
        // trigger the alarm and hold its irq until reset by the user
        if (sim_ctx.temp_mC >= (1000 * ((sim_cfg.temp_target + sim_cfg.temp_hysteresis + 10)))) {
            bool is_enabled = (gpio->int_en.f.en & (1 << 8));
            bool is_pending = (gpio->int_sts.f.status & (1 << 8));

            if (is_enabled && !is_pending) {
                gpio->in.f.alarm = 1;               // The Over-Temp Alarm bit is set by the real HW
                                                    // when the temperature exceeds the threshold
                gpio->int_sts.f.status |= (1 << 8); // Latch the alarm interrupt
                done = true;                        // signal only once (see below for the normal
                                                    // sensor update)
                // Delay next sensor update (below) to sensor_latency after alarm trigger
                // to allow temperature to drop a bit (see
                // run_temperature_regulation)
                if (ms_counter > sim_ctx.phy.sensor_latency_ms >> 1)
                    ms_counter = 0;
                // Trigger the Alarm IRQ via Unix Signal (SIGIO)
                kill(getpid(), SIGIO);
                LOG_SYS_INFO("Over-Temp Alarm Triggered at %d°C", (sim_ctx.temp_mC / 1000));
            }
        } else {
            // Temperature dropped below threshold: HW clears the input pin alarm
            gpio->in.f.alarm = 0;
        }

        sim_ctx.state.energy_balance = sim_ctx.state.net_heat_power - sim_ctx.state.cooling_power;
        sim_ctx.temp_mC += (sim_ctx.state.energy_balance / (int32_t)sim_ctx.phy.thermal_mass);

        // --- SENSOR: Update Input Register every 30ms ---
        if (ms_counter >= sim_ctx.phy.sensor_latency_ms && !done) {
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
