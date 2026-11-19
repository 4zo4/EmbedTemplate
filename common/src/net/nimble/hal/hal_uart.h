#ifndef H_HAL_UART_
#define H_HAL_UART_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*hal_uart_tx_char)(void *arg);
typedef void (*hal_uart_tx_done)(void *arg);
typedef int (*hal_uart_rx_char)(void *arg, uint8_t byte);

enum hal_uart_parity {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_ODD = 1,
    HAL_UART_PARITY_EVEN = 2
};

enum hal_uart_flow_ctl {
    HAL_UART_FLOW_CTL_NONE = 0,
    HAL_UART_FLOW_CTL_RTS_CTS = 1
};

int  hal_uart_init_cbs(int port, hal_uart_tx_char tx_func, hal_uart_tx_done tx_done, hal_uart_rx_char rx_func, void *arg);
int  hal_uart_config(int port, int32_t speed, uint8_t databits, uint8_t stopbits, enum hal_uart_parity parity, enum hal_uart_flow_ctl flow_ctl);
void hal_uart_start_tx(int port);
int  hal_uart_close(int port);

#ifdef __cplusplus
}
#endif

#endif /* H_HAL_UART_ */
