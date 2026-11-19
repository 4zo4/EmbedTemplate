#ifndef _NIMBLE_NPL_OS_LOG_H_
#define _NIMBLE_NPL_OS_LOG_H_

#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "utils.h"

#define _BLE_STRIP_BLE_HS_LOG        BLE_HS
#define _BLE_STRIP_BLE_EATT_LOG      BLE_EATT
#define _BLE_STRIP_BLE_LL_LOG        BLE_LL
#define _BLE_STRIP_BLE_TRANSPORT_LOG BLE_TRANSPORT
#define _BLE_STRIP_BLE_MESH_LOG      BLE_MESH
#define MODULE_NAME(mod) _BLE_NPL_LOG_CAT(_BLE_STRIP_, mod)

#define _LOG_LEVEL_DEBUG    DEBUG
#define _LOG_LEVEL_INFO     INFO
#define _LOG_LEVEL_WARN     WARNING
#define _LOG_LEVEL_ERROR    ERROR
#define _LOG_LEVEL_CRITICAL CRITICAL
#define LOG_LEVEL(lvl)     _BLE_NPL_LOG_CAT(_LOG_LEVEL_, lvl)

#define LOG_DISPATCH(lvl, fmt, ...) \
    _BLE_NPL_LOG_CAT(LOG_ENTITY_, LOG_LEVEL(lvl))(ID_SYS(ENT_BLE), \
        "[" STR(MODULE_NAME(BLE_NPL_LOG_MODULE)) "] " fmt, ##__VA_ARGS__)

#include "nimble/nimble_npl_module_log.h"

#endif  /* _NIMBLE_NPL_OS_LOG_H_ */