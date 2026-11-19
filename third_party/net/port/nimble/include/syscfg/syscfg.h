#ifndef H_EXTERN_PORT_MYNEWT_SYSCFG_
#define H_EXTERN_PORT_MYNEWT_SYSCFG_

#define MYNEWT_VAL_BLE_HOST                  (1)
#define MYNEWT_VAL_BLE_CONTROLLER            (0)

/* Disable Security Manager / mbedTLS */
#define MYNEWT_VAL_BLE_SM_LEGACY             (0)
#define MYNEWT_VAL_BLE_SM_SC                 (0)
#define MYNEWT_VAL_BLE_CRYPTO_STACK_MBEDTLS  (0)
#define MYNEWT_VAL_TINYCRYPT                 (0)

/* HCI Transport: Host on this MCU (native) <-> External Controller over UART (uart_ll) */
#define MYNEWT_VAL_BLE_TRANSPORT_HS__native  (1)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__uart    (0)

#define MYNEWT_VAL_BLE_TRANSPORT_LL__native  (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__socket  (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__uart_ll (1)

/* Link Layer UART Transport Settings (from nimble/transport/uart_ll/syscfg.yml) */
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_PORT                 (0)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_BAUDRATE             (1000000)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_DATA_BITS            (8)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_STOP_BITS            (1)

#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_PARITY__none         (1)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_PARITY__odd          (0)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_PARITY__even         (0)

#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_FLOW_CONTROL__off    (0)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_FLOW_CONTROL__rtscts (1)

#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_TX_BUF_SIZE          (64)
#define MYNEWT_VAL_BLE_TRANSPORT_UART_LL_RX_BUF_SIZE          (64)

/* GATT Server Static Table Capacities */
#define MYNEWT_VAL_BLE_GATTS_MAX_SVCS (4)
#define MYNEWT_VAL_BLE_GATTS_MAX_CHRS (16)
#define MYNEWT_VAL_BLE_GATTS_MAX_DSCS (8)

#define MYNEWT_VAL_BLE_LL_CFG_FEAT_LL_PRIVACY (0)
#define MYNEWT_VAL_BLE_HOST_BASED_PRIVACY (0)

/* Disable dynamic/guard checks */
#define MYNEWT_VAL_OS_MEMPOOL_CHECK  (0)
#define MYNEWT_VAL_OS_MEMPOOL_POISON (0)
#define MYNEWT_VAL_OS_MEMPOOL_GUARD  (0)

/* Remaining settings are from the original syscfg.h */
#include_next "syscfg/syscfg.h"

#endif /* H_EXTERN_PORT_MYNEWT_SYSCFG_ */
