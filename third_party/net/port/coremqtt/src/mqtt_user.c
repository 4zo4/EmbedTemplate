#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "core_mqtt.h"
#include "core_mqtt_log.h"
#include "transport_interface.h"
#include "mqtt_ble_transport.h"

#define MQTT_NET_BUFFER_SIZE (1024U)
#define MQTT_KEEP_ALIVE_SEC (60U)
#define MQTT_CONNACK_TIMEOUT_MS (5000U)
#define MQTT_CLIENT_ID "mqttBle"

#define MQTT_PUB_TOPIC "embed/telemetry"
#define MQTT_SUB_TOPIC "embed/cmd"
#define MQTT_PUB_INTERVAL (5000U) // 5 seconds 

static MQTTContext_t mqtt_ctx;
static uint8_t       mqtt_net_buf[MQTT_NET_BUFFER_SIZE];
static uint16_t      packet_id = 1U;

static uint32_t mqtt_get_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool mqtt_event_cb(MQTTContext_t *ctx, MQTTPacketInfo_t *packet_info,
                          MQTTDeserializedInfo_t *deserialized_info, MQTTSuccessFailReasonCode_t *reason_code,
                          MQTTPropBuilder_t *prop_builder, MQTTPropBuilder_t *ack_prop_builder)
{
    (void)ctx;
    (void)reason_code;
    (void)prop_builder;
    (void)ack_prop_builder;

    if ((packet_info->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        (void)deserialized_info->pPublishInfo;
    }

    return true;
}

static int mqtt_session_connect(void)
{
    MQTTConnectInfo_t conn_info = {0};
    MQTTStatus_t      status;
    bool              session_present = false;

    conn_info.cleanSession = true;
    conn_info.pClientIdentifier = MQTT_CLIENT_ID;
    conn_info.clientIdentifierLength = (uint16_t)strlen(MQTT_CLIENT_ID);
    conn_info.keepAliveSeconds = MQTT_KEEP_ALIVE_SEC;

    status = MQTT_Connect(&mqtt_ctx, &conn_info, NULL,                     /* pWillInfo */
                          MQTT_CONNACK_TIMEOUT_MS, &session_present, NULL, /* pConnectArgs (MQTT 5.0 properties) */
                          NULL);                                           /* pConnackArgs (MQTT 5.0 properties) */

    return (status == MQTTSuccess) ? 0 : -1;
}

static void mqtt_publish_test(void)
{
    MQTTPublishInfo_t pub_info = {0};
    const char       *payload = "{\"status\":\"alive\"}";

    pub_info.qos = MQTTQoS0;
    pub_info.pTopicName = MQTT_PUB_TOPIC;
    pub_info.topicNameLength = (uint16_t)strlen(MQTT_PUB_TOPIC);
    pub_info.pPayload = payload;
    pub_info.payloadLength = strlen(payload);

    MQTT_Publish(&mqtt_ctx, &pub_info, 0U, NULL);
}

static void
    mqtt_subscribe_cmd(void)
{
    MQTTSubscribeInfo_t sub_info = {0};

    sub_info.qos = MQTTQoS0;
    sub_info.pTopicFilter = MQTT_SUB_TOPIC;
    sub_info.topicFilterLength = (uint16_t)strlen(MQTT_SUB_TOPIC);

    MQTT_Subscribe(&mqtt_ctx, &sub_info, 1U, packet_id++, NULL);
}

int mqtt_init(void)
{
    TransportInterface_t *transport;
    MQTTFixedBuffer_t     fixed_buf;
    MQTTStatus_t          status;

    transport = mqtt_ble_transport_get_interface();
    if (!transport) {
        return -1;
    }

    fixed_buf.pBuffer = mqtt_net_buf;
    fixed_buf.size = sizeof(mqtt_net_buf);

    status = MQTT_Init(&mqtt_ctx, transport, mqtt_get_time_ms, mqtt_event_cb, &fixed_buf);

    return (status == MQTTSuccess) ? 0 : -1;
}

void mqtt_run(void)
{
    uint32_t     last_pub_ms;
    uint32_t     cur_time_ms;
    uint32_t     session_id = 0;
    MQTTStatus_t status;
    bool         session_active = false;

    for (;;) {
        // Wait until BLE peer connects & subscribes to TX notifications
        if (!mqtt_ble_transport_is_connected()) {
            session_active = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Establish MQTT session once BLE link is subscribed
        if (!session_active) {
            if (mqtt_session_connect() != 0) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            mqtt_subscribe_cmd();
            session_active = true;
            last_pub_ms = 0U;
            LogInfo(("MQTT session %u established over BLE transport.", ++session_id));
        }

        cur_time_ms = mqtt_get_time_ms();
        if ((cur_time_ms - last_pub_ms) >= MQTT_PUB_INTERVAL) {
            mqtt_publish_test();
            last_pub_ms = cur_time_ms;
            LogInfo(("MQTT session %u publish status alive.", session_id));
        }

        // Process incoming packets and keep-alive pings
        status = MQTT_ProcessLoop(&mqtt_ctx);
        if (status != MQTTSuccess && status != MQTTNeedMoreBytes) {
            session_active = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
