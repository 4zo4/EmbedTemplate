
#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_uart.h"

int test_uart_init(char *args)
{
    (void)args;
    LOG_UART_TEST_INFO("Test UART init");
    for (int i = 0; i < 22; i++)
        LOG_UART_TEST_INFO("Msg %d: %s", i, (i % 2) ? "Short UART msg" : "A long UART message to stumble log");
    return 0; // Return 0 on success
}

int test_uart_send_receive(char *args)
{
    // Code to test UART send and receive functionality
    (void)args;
    LOG_UART_TEST_INFO("Test UART send and receive functionality");
    return 0; // Return 0 on success
}

int test_uart_baud_rate(char *args)
{
    // Code to test UART baud rate settings
    (void)args;
    return 0; // Return 0 on success
}

test_desc_t UART_tests[] = {
    {"UART_Init",  test_uart_init,         true },
    {"UART_TxRx",  test_uart_send_receive, true },
    {"UART_Rate",  test_uart_baud_rate,    false},
    {"UART_test4", nullptr,                true },
};

static_assert(sizeof(UART_tests) / sizeof(UART_tests[0]) == UART_TEST_NUM, "UART tests out of range");
