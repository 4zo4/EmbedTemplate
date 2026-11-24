#ifndef CORE_MQTT_LOG_H_
#define CORE_MQTT_LOG_H_

#include "log.h"
#include "log_marker.h"

#define _MQTT_LOG_ERROR(fmt, ...) LOG_ENTITY_ERROR(ID_SYS(ENT_MQTT), fmt, ##__VA_ARGS__)
#define _MQTT_LOG_WARN(fmt, ...) LOG_ENTITY_WARNING(ID_SYS(ENT_MQTT), fmt, ##__VA_ARGS__)
#define _MQTT_LOG_INFO(fmt, ...) LOG_ENTITY_INFO(ID_SYS(ENT_MQTT), fmt, ##__VA_ARGS__)
#define _MQTT_LOG_DEBUG(fmt, ...) LOG_ENTITY_DEBUG(ID_SYS(ENT_MQTT), fmt, ##__VA_ARGS__)

#define LogError(msg) _MQTT_LOG_ERROR msg
#define LogWarn(msg) _MQTT_LOG_WARN msg
#define LogInfo(msg) _MQTT_LOG_INFO msg
#define LogDebug(msg) _MQTT_LOG_DEBUG msg

#endif // CORE_MQTT_LOG_H_
