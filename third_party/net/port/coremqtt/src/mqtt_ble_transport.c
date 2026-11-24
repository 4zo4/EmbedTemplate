#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h" // FreeRTOS stream buffer

#include "core_mqtt.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "transport_interface.h"
#include "mqtt_ble_transport.h"

#define BLE_MQTT_RX_BUFFER_SIZE (1024U)
#define BLE_MQTT_RECV_TIMEOUT_MS (10U)
#define BLE_ATT_NOTIFY_OVERHEAD (3U)

typedef struct NetworkContext {
    uint16_t conn_handle;
    uint16_t tx_attr_handle; /* Characteristic value handle for outgoing data */
    uint8_t  role;           /* NimBLE role: BLE_GAP_ROLE_MASTER or BLE_GAP_ROLE_SLAVE */
    bool     is_connected;

    StreamBufferHandle_t rx_stream;
    StaticStreamBuffer_t rx_stream_buf;
    uint8_t              rx_storage[BLE_MQTT_RX_BUFFER_SIZE];
} NetworkContext_t;

int nimble_host_add_gatt_svcs(const struct ble_gatt_svc_def *svcs, ble_gap_event_fn *gap_cb, const char *dev_name);

static int32_t mqtt_ble_transport_send(NetworkContext_t *ctx, const void *buf, size_t len);
static int32_t mqtt_ble_transport_recv(NetworkContext_t *ctx, void *buf, size_t len);

extern const struct ble_gatt_svc_def mqtt_gatt_svcs[];

static NetworkContext_t ble_net_ctx = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .tx_attr_handle = 0U,
    .role = BLE_GAP_ROLE_SLAVE,
    .is_connected = false,
    .rx_stream = NULL,
};

static TransportInterface_t mqtt_transport = {
    .pNetworkContext = &ble_net_ctx,
    .recv = mqtt_ble_transport_recv,
    .send = mqtt_ble_transport_send,
    .writev = NULL,
};

int mqtt_ble_transport_cfg(void)
{
    return nimble_host_add_gatt_svcs(mqtt_gatt_svcs, mqtt_gatt_on_gap_event, "mqttGap");
}

int mqtt_ble_transport_init(void)
{
    if (ble_net_ctx.rx_stream) {
        xStreamBufferReset(ble_net_ctx.rx_stream);
        return 0;
    }

    ble_net_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_net_ctx.tx_attr_handle = 0U;
    ble_net_ctx.role = BLE_GAP_ROLE_SLAVE;
    ble_net_ctx.is_connected = false;

    ble_net_ctx.rx_stream = xStreamBufferCreateStatic(
        sizeof(ble_net_ctx.rx_storage),
        1U,
        ble_net_ctx.rx_storage,
        &ble_net_ctx.rx_stream_buf
    );

    if (!ble_net_ctx.rx_stream) {
        return -1;
    }

    return 0;
}

TransportInterface_t *mqtt_ble_transport_get_interface(void)
{
    return ble_net_ctx.rx_stream ? &mqtt_transport : NULL;
}

void mqtt_ble_transport_set_connection(uint16_t conn_handle, uint16_t tx_attr_handle, uint8_t role, bool is_connected)
{
    ble_net_ctx.conn_handle = conn_handle;
    ble_net_ctx.tx_attr_handle = tx_attr_handle;
    ble_net_ctx.role = role;
    ble_net_ctx.is_connected = is_connected;

    if (!is_connected && ble_net_ctx.rx_stream) {
        xStreamBufferReset(ble_net_ctx.rx_stream);
    }
}

bool mqtt_ble_transport_is_connected(void)
{
    return ble_net_ctx.is_connected;
}

size_t mqtt_ble_transport_on_rx_data(const struct os_mbuf *om)
{
    const struct os_mbuf *cur;
    size_t                total_written = 0U;
    size_t                written;

    if (!ble_net_ctx.rx_stream || !om) {
        return 0U;
    }

    for (cur = om; cur; cur = SLIST_NEXT(cur, om_next)) {
        if (cur->om_len > 0U) {
            written = xStreamBufferSend(ble_net_ctx.rx_stream, cur->om_data, cur->om_len, 0U);
            total_written += written;
            if (written < cur->om_len) {
                break;
            }
        }
    }

    return total_written;
}

static int32_t mqtt_ble_transport_send(NetworkContext_t *ctx, const void *buf, size_t len)
{
    const uint8_t  *bytes;
    struct os_mbuf *om;
    size_t          bytes_sent = 0U;
    size_t          max_payload;
    size_t          chunk_len;
    uint16_t        mtu;
    int             rc;

    if (!ctx || !buf || len == 0U) {
        return -1;
    }

    if (!ctx->is_connected || ctx->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return -1;
    }

    mtu = ble_att_mtu(ctx->conn_handle);
    max_payload = (mtu > BLE_ATT_NOTIFY_OVERHEAD) ?
        (mtu - BLE_ATT_NOTIFY_OVERHEAD) :
        20U;
    bytes = (const uint8_t *)buf;

    while (bytes_sent < len) {
        chunk_len = len - bytes_sent;
        if (chunk_len > max_payload) {
            chunk_len = max_payload;
        }

        om = ble_hs_mbuf_from_flat(bytes + bytes_sent, chunk_len);
        if (!om) {
            return (bytes_sent > 0U) ? (int32_t)bytes_sent : -1;
        }

        if (ctx->role == BLE_GAP_ROLE_SLAVE) {
            rc = ble_gatts_notify_custom(ctx->conn_handle, ctx->tx_attr_handle, om);
        } else {
            rc = ble_gattc_write_no_rsp(ctx->conn_handle, ctx->tx_attr_handle, om);
        }

        if (rc != 0) {
            return (bytes_sent > 0U) ? (int32_t)bytes_sent : -1;
        }

        bytes_sent += chunk_len;
    }

    return (int32_t)bytes_sent;
}

static int32_t mqtt_ble_transport_recv(NetworkContext_t *ctx, void *buf, size_t len)
{
    size_t bytes_received;

    if (!ctx || !buf || len == 0U) {
        return -1;
    }

    if (!ctx->is_connected || !ctx->rx_stream) {
        return -1;
    }

    bytes_received = xStreamBufferReceive(
        ctx->rx_stream,
        buf,
        len,
        pdMS_TO_TICKS(BLE_MQTT_RECV_TIMEOUT_MS)
    );

    return (int32_t)bytes_received;
}
