#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qemu/log.h"

#define TYPE_MMIO_USER "mmio-user"
OBJECT_DECLARE_SIMPLE_TYPE(MmioUserState, MMIO_USER)

enum {
    MMIO_USER_READ  = 1,
    MMIO_USER_WRITE = 2,
};

typedef struct __attribute__((packed)) {
    uint32_t op;
    uint32_t size;
    uint64_t addr;
    uint64_t data;
} MmioUserMsg;

struct MmioUserState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    CharFrontend chr;
    uint64_t base;
    uint64_t size;
};

static uint64_t mmio_user_read(void *opaque, hwaddr addr, unsigned size)
{
    MmioUserState *s = opaque;
    MmioUserMsg msg = {
        .op   = MMIO_USER_READ,
        .size = size,
        .addr = addr,
        .data = 0,
    };

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_log_mask(LOG_GUEST_ERROR, "mmio-user: backend not connected\n");
        return 0;
    }

    qemu_chr_fe_write_all(&s->chr, (uint8_t *)&msg, sizeof(msg));
    qemu_chr_fe_read_all(&s->chr, (uint8_t *)&msg, sizeof(msg));
    return msg.data;
}

static void mmio_user_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MmioUserState *s = opaque;
    MmioUserMsg msg = {
        .op   = MMIO_USER_WRITE,
        .size = size,
        .addr = addr,
        .data = val,
    };

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_log_mask(LOG_GUEST_ERROR, "mmio-user: backend not connected\n");
        return;
    }

    qemu_chr_fe_write_all(&s->chr, (uint8_t *)&msg, sizeof(msg));
    qemu_chr_fe_read_all(&s->chr, (uint8_t *)&msg, sizeof(msg));
}

static const MemoryRegionOps mmio_user_ops = {
    .read = mmio_user_read,
    .write = mmio_user_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void mmio_user_realize(DeviceState *dev, Error **errp)
{
    MmioUserState *s = MMIO_USER(dev);

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(errp, "mmio-user: 'chardev' property must be set");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &mmio_user_ops, s,
                          TYPE_MMIO_USER, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    if (s->base != UINT64_MAX) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, s->base);
    }
}

static const Property mmio_user_properties[] = {
    DEFINE_PROP_CHR("chardev", MmioUserState, chr),
    DEFINE_PROP_UINT64("base", MmioUserState, base, UINT64_MAX),
    DEFINE_PROP_UINT64("size", MmioUserState, size, 0x10000000),
};

static void mmio_user_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mmio_user_realize;
    device_class_set_props(dc, mmio_user_properties);
    dc->user_creatable = true;
}

static const TypeInfo mmio_user_info = {
    .name          = TYPE_MMIO_USER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MmioUserState),
    .class_init    = mmio_user_class_init,
};

static void mmio_user_register_types(void)
{
    type_register_static(&mmio_user_info);
}

type_init(mmio_user_register_types)
