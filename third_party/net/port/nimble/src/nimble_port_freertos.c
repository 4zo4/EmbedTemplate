#include <stddef.h>
#include <stdio.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#if defined(NIMBLE_CFG_CONTROLLER) && (NIMBLE_CFG_CONTROLLER == 1)
#error "NIMBLE_CFG_CONTROLLER unsupported"
#endif

void ble_store_config_init(void);
static void ble_app_advertise(uint8_t addr_type);

#define NIMBLE_HEAP_SIZE 2048
#define NIMBLE_ADDR_TYPE_INVALID 0xFF
#define NIMBLE_MAX_SVC_DEF_TABLES 8

alignas(8) static uint8_t nimble_heap[NIMBLE_HEAP_SIZE];
static const void *nimble_gatts_svc_defs_buf[NIMBLE_MAX_SVC_DEF_TABLES];
static size_t nimble_heap_off = 0;
static uint32_t free_count = 0;
static uint8_t own_addr_type = NIMBLE_ADDR_TYPE_INVALID;

void *
__wrap_malloc(size_t size)
{
    size_t aligned_size = (size + 7u) & ~7u;
    if (aligned_size == 0 || (nimble_heap_off + aligned_size) > sizeof(nimble_heap)) {
        return NULL;
    }

    void *ptr = &nimble_heap[nimble_heap_off];
    nimble_heap_off += aligned_size;
    return ptr;
}

void
__wrap_free(void *ptr)
{
    (void)ptr;
    free_count++;
}

void *
__wrap_realloc(void *ptr, size_t new_size)
{
    if (new_size == 0 || new_size > sizeof(nimble_gatts_svc_defs_buf)) {
        return NULL;
    }

    if (ptr == NULL || ptr == (void *)nimble_gatts_svc_defs_buf) {
        return (void *)nimble_gatts_svc_defs_buf;
    }

    return NULL;
}

static int
ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        BLE_HS_LOG(INFO, "GAP connect: status=%d handle=%u",
                   event->connect.status, event->connect.conn_handle);
        if (event->connect.status != 0) {
            ble_app_advertise(own_addr_type);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        BLE_HS_LOG(INFO, "GAP disconnect: reason=%d",
                   event->disconnect.reason);
        ble_app_advertise(own_addr_type);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        BLE_HS_LOG(INFO, "GAP advertising complete: reason=%d",
                   event->adv_complete.reason);
        ble_app_advertise(own_addr_type);
        return 0;

    default:
        return 0;
    }
}

static void
ble_app_advertise(uint8_t addr_type)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    const char *name;
    int rc;

    if (addr_type == NIMBLE_ADDR_TYPE_INVALID) {
        BLE_HS_LOG(ERROR, "Cannot advertise: invalid addr_type");
        return;
    }

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        BLE_HS_LOG(ERROR, "Failed to set gap adv fields: rc=%d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        BLE_HS_LOG(ERROR, "Failed to start advertising: rc=%d", rc);
        return;
    }

    BLE_HS_LOG(INFO, "Advertising started as '%s'", name);
}

static void
ble_on_sync(void)
{
    int rc;
    uint8_t addr_val[6] = {0};
    uint8_t addr_type;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        BLE_HS_LOG(ERROR, "Failed to ensure address: rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) {
        BLE_HS_LOG(ERROR, "Failed to determine address type: rc=%d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(addr_type, addr_val, NULL);
    if (rc == 0) {
        BLE_HS_LOG(INFO, "Host synced: addr_type=%u addr=%02X:%02X:%02X:%02X:%02X:%02X",
                addr_type, addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);
    }

    own_addr_type = addr_type;
    ble_app_advertise(addr_type);
}

static void
ble_on_reset(int reason)
{
    BLE_HS_LOG(WARN,"Host reset: reason=%d", reason);
}

void
nimble_port_freertos_init(TaskFunction_t host_task_fn)
{
    (void)host_task_fn;
    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set("purpleGap");
}
