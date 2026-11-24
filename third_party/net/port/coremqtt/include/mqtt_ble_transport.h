#ifndef MQTT_BLE_TRANSPORT_H
#define MQTT_BLE_TRANSPORT_H

struct os_mbuf;
struct ble_gap_event;

int mqtt_ble_transport_init(void);
int mqtt_ble_transport_cfg(void);

TransportInterface_t *mqtt_ble_transport_get_interface(void);

void mqtt_ble_transport_set_connection(uint16_t conn_handle,
                                       uint16_t tx_attr_handle,
                                       uint8_t role,
                                       bool is_connected);
bool mqtt_ble_transport_is_connected(void);

size_t mqtt_ble_transport_on_rx_data(const struct os_mbuf *om);

int mqtt_gatt_on_gap_event(struct ble_gap_event *event, void *arg);



#endif /* MQTT_BLE_TRANSPORT_H */
