#include <stddef.h>
#include <stdint.h>

#include "hal_uart.h"
#include "init.h"

// Uart Tx used for NimBLE HCI, HCI Uart index is 1 (2nd uart, console uart is 0)
int uart_tx(int n, int c);

// Uart Rx used for NimBLE HCI
extern int (*uart_rx)(void *arg, uint8_t byte);

// -- Globals --

extern void *cb_arg; // argument for NimBLE HCI Uart Rx/Tx APIs

static hal_uart_tx_char nimble_tx_func = nullptr;
static hal_uart_tx_done nimble_tx_done = nullptr;

// -- End of globals --

// -- NimBLE HAL APIs --

int hal_uart_init_cbs(int port, hal_uart_tx_char tx_func, hal_uart_tx_done tx_done, hal_uart_rx_char rx_func, void *arg)
{
    (void)port;
    if (initialized & BLE_INITIALIZED)
        return -1;

    cb_arg = arg;
    nimble_tx_func = tx_func;
    nimble_tx_done = tx_done;
    uart_rx = rx_func;

    initialized |= BLE_INITIALIZED;
    return 0;
}

int hal_uart_config(int port, int32_t speed, uint8_t databits, uint8_t stopbits, enum hal_uart_parity parity, enum hal_uart_flow_ctl flow_ctl)
{
    (void)port;
    (void)speed;
    (void)databits;
    (void)stopbits;
    (void)parity;
    (void)flow_ctl;
    return 0;
}

int hal_uart_close(int port)
{
    (void)port;
    return 0;
}

void hal_uart_start_tx(int port)
{
    (void)port;
    int c;
    while ((c = nimble_tx_func(cb_arg)) >= 0) {
        uart_tx(1, c);
    }
    if (nimble_tx_done != nullptr)
        nimble_tx_done(cb_arg);
}
