
#include <stdint.h>

#include "gpio_demo_regs.h"
#include "gpio.h"
#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_gpio.h"

// prototypes without include file
bool sim_pause(void);
void sim_resume(void);

int test_gpio_init(char *args)
{
    (void)args;
    volatile gpio_ctrl_t *gpio = gpio_get_regs();
    int                   ret = 0;

#ifdef ENABLE_RTOS
    // Pause hardware simulation
    if (!sim_pause()) {
        LOG_GPIO_TEST_WARNING("Can't pause GPIO");
        return -1;
    }
#endif
    // backup GPIO controller state
    alignas(8) static volatile gpio_ctrl_t gpio_bkp;
    gpio_bkp = *gpio; // backup current state

    LOG_GPIO_TEST_INFO("Test GPIO init");

    // Initialize GPIO controller to a known state
    gpio_init_controller(gpio);

    // Verify reset values
    if (gpio->dir.f.pin_dir != 0x00)
        ret = -1;
    if (gpio->wdt_cfg.f.en != 0)
        ret = -1;
    if (gpio->wdt_val.f.timer != 0)
        ret = -1;

    for (int i = 0; i < 21; i++)
        LOG_GPIO_TEST_INFO("Msg %d: %s", i, (i % 2) ? "Short GPIO msg" : "A long GPIO message to stumble log");

    // Restore GPIO state after testing
    *gpio = gpio_bkp;
#ifdef ENABLE_RTOS
    sim_resume();
#endif
    LOG_GPIO_TEST_INFO("Test GPIO init done");

    return ret;
}

int test_gpio_functionality(char *args)
{
    (void)args;
    LOG_GPIO_TEST_INFO("Test GPIO functionality");
    return 0; // Return 0 on success
}

test_desc_t GPIO_tests[] = {
    {"GPIO_Init",  test_gpio_init,          true},
    {"GPIO_Func",  test_gpio_functionality, true},
    {"GPIO_test3", nullptr,                 true},
    {"GPIO_test4", nullptr,                 true},
    {"GPIO_test5", nullptr,                 true},
    {"GPIO_test6", nullptr,                 true},
    {"GPIO_test7", nullptr,                 true},
};

static_assert(sizeof(GPIO_tests) / sizeof(GPIO_tests[0]) == GPIO_TEST_NUM, "GPIO tests out of range");
