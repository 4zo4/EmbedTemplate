#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "transport_interface.h"
#include "mqtt_ble_transport.h"

// Populated by NimBLE when mqtt_gatt_svcs is registered
static uint16_t mqtt_tx_chr_val_handle;

/*
 * Custom 128-bit UUIDs for MQTT-over-GATT Service
 * Service: 00000001-ccbb-aa99-8877-665544332210
 * RX Char: 00000002-ccbb-aa99-8877-665544332210 (Peer -> Device: Write / Write No Rsp)
 * TX Char: 00000003-ccbb-aa99-8877-665544332210 (Device -> Peer: Notify)
 */
static const ble_uuid128_t gatt_svr_svc_mqtt_uuid =
    BLE_UUID128_INIT(0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0x01, 0x00, 0x00, 0x00);

static const ble_uuid128_t gatt_svr_chr_mqtt_rx_uuid =
    BLE_UUID128_INIT(0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0x02, 0x00, 0x00, 0x00);

static const ble_uuid128_t gatt_svr_chr_mqtt_tx_uuid =
    BLE_UUID128_INIT(0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0x03, 0x00, 0x00, 0x00);

static int mqtt_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    size_t expected;
    size_t written;

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        expected = OS_MBUF_PKTLEN(ctxt->om);
        written = mqtt_ble_transport_on_rx_data(ctxt->om);
        if (written < expected) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

const struct ble_gatt_svc_def mqtt_gatt_svcs[] = {
    {
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &gatt_svr_svc_mqtt_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* RX: Peer writes MQTT packets to device */
                .uuid = &gatt_svr_chr_mqtt_rx_uuid.u,
                .access_cb = mqtt_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* TX: Device notifies MQTT packets to peer */
                .uuid = &gatt_svr_chr_mqtt_tx_uuid.u,
                .access_cb = mqtt_gatt_access_cb,
                .val_handle = &mqtt_tx_chr_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, /* No more characteristics in this service */
            }
        },
     },
    {
     0, /* No more services */
    },
};

int mqtt_gatt_on_gap_event(struct ble_gap_event *event, void *arg)
{
    bool subscribed;

    (void)arg;

    if (!event) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == mqtt_tx_chr_val_handle) {
            subscribed = (event->subscribe.cur_notify != 0U);
            mqtt_ble_transport_set_connection(event->subscribe.conn_handle, mqtt_tx_chr_val_handle, BLE_GAP_ROLE_SLAVE, subscribed);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        mqtt_ble_transport_set_connection(BLE_HS_CONN_HANDLE_NONE, 0U, BLE_GAP_ROLE_SLAVE, false);
        break;

    default:
        break;
    }

    return 0;
}
