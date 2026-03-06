#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "log.h"
#include "log_marker.h"
#include "term_codes.h"
#include "utils.h"

uint64_t get_timestamp48(void); // prototype without include file

#define LOG_BUF_SIZE 4096
#define LOG_BUF_MASK (LOG_BUF_SIZE - 1)

static_assert((LOG_BUF_SIZE & LOG_BUF_MASK) == 0, "LOG_BUF_SIZE must be a power of 2");
static_assert((LOG_BUF_SIZE % 4) == 0, "LOG_BUF_SIZE must be 4-byte aligned");

typedef struct log_ring_s {
    uint8_t             *data;
    uint32_t             size;
    uint32_t             mask;
    atomic_uint_fast32_t head;
    atomic_uint_fast32_t tail;
    atomic_bool          dirty;
} log_ring_t;

typedef struct __attribute__((packed)) log_hdr_s {
    uint8_t  tag;     // Log tag marker
    uint8_t  lvl;     // Log level
    uint8_t  dom;     // Domain
    uint8_t  ent;     // Entity
    uint8_t  len;     // Payload (message) length (1 - 255)
    uint8_t  pad;     // Pad length for log entry (hdr + payload) 4-byte alignment
    uint16_t ts_high; // Upper 16 bits of 48-bit timestamp
    uint32_t ts_low;  // Lower 32 bits of 48-bit timestamp
} log_hdr_t;          // total size 12 bytes (8 + 4) to allow for optimized u64 and u32
                      // access

#define HDR_SIZE sizeof(log_hdr_t)
static_assert(HDR_SIZE == 12, "Log header size must be 12 bytes");

typedef struct log_buf_s {
    uint32_t guard_start;
    alignas(8) uint8_t data[LOG_BUF_SIZE];
    uint32_t guard_end;
} log_buf_t;

typedef struct log_stats_atomic_s {
    atomic_uint_least64_t sum;
    atomic_uint_least32_t drop_cnt;
} log_stats_atomic_t;

// log tag markers
#define LOG_TAG_SEAL 0xAC
#define LOG_TAG_FREE 0xDF
#define MAX_DATA_SIZE 255

// -- Globals --

__attribute__((section(".sram_log_buf"), used))

static log_buf_t log_buf = {.guard_start = 0xDEADBEEF, .guard_end = 0xDEADBEEF};

static uint8_t            log_level[LOG_LEVEL_SIZE]; // log level per entity + domain
static log_ring_t         log_ring = {.data = log_buf.data, .size = LOG_BUF_SIZE, .mask = LOG_BUF_MASK, .dirty = false};
static log_stats_atomic_t log_stats = {0};

// -- End of globals --

static inline void copy_u64(void *dst, const void *src, uint32_t len)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    // 64-bit (QWORD) Copy - High Speed
    while (len >= 8) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8;
        s += 8;
        len -= 8;
    }

    // 32-bit (DWORD) Copy - Remainder
    if (len >= 4) {
        *(uint32_t *)d = *(const uint32_t *)s;
        d += 4;
        s += 4;
        len -= 4;
    }

    // 16-bit (WORD) Copy - Remainder
    if (len >= 2) {
        *(uint16_t *)d = *(const uint16_t *)s;
        d += 2;
        s += 2;
        len -= 2;
    }

    // Final Byte
    if (len > 0) {
        *d = *s;
    }
}

static void copy_to_ring(uint32_t start_pos, const void *src, uint32_t len)
{
    uint32_t hid = start_pos & log_ring.mask; // head index

    // calculate how much space is left before we hit the physical end of the
    // array
    uint32_t space_to_end = log_ring.size - hid;

    if (len <= space_to_end) { // [[likely]]
        // full message fits linearly
        // use optimized copy_u64 for max QWORD throughput
        copy_u64(&log_ring.data[hid], src, len);
    } else {
        // message must be split
        // fill the remaining space at the end
        copy_u64(&log_ring.data[hid], src, space_to_end);

        // wrap to the beginning and copy the rest
        uint32_t remaining = len - space_to_end;
        copy_u64(&log_ring.data[0], (uint8_t *)src + space_to_end, remaining);
    }
}

static void copy_from_ring(void *dst, uint32_t start_pos, uint32_t len)
{
    uint32_t tid = start_pos & log_ring.mask; // tail index
    uint32_t space_to_end = log_ring.size - tid;

    if (len <= space_to_end) {
        // data is linear in the buffer
        copy_u64(dst, &log_ring.data[tid], len);
    } else {
        // data is wrapped; split the read
        // read from tail to the physical end of array
        copy_u64(dst, &log_ring.data[tid], space_to_end);

        // read remaining from the physical start of array
        uint32_t remaining = len - space_to_end;
        copy_u64((uint8_t *)dst + space_to_end, &log_ring.data[0], remaining);
    }
}

static inline void zero_padding(uint32_t end_pos, uint8_t pad_len)
{
    uint32_t mask = log_ring.mask;

    // For an optmized build the compiler can optimize three
    // sequential 8-bit writes into a single 16-bit or 24-bit store.
    switch (pad_len) {
    case 3:
        log_ring.data[(end_pos + 2) & mask] = 0;
        [[fallthrough]];
    case 2:
        log_ring.data[(end_pos + 1) & mask] = 0;
        [[fallthrough]];
    case 1:
        log_ring.data[end_pos & mask] = 0;
        [[fallthrough]];
    default:
        break;
    }
}

void log_dispatch(uint8_t domain, uint8_t entity, uint8_t level, const char *fmt, va_list args)
{
    alignas(8) char buf[256];
    int             len = vsnprintf(buf, sizeof(buf), fmt, args);
    // clang-format off
    if (len <= 0) {
        printf("\r\n%s: " UI_COLOR_RED "[LOG] Error: " UI_STYLE_RESET "invalid log message length %d\n", __func__, len);
        return;
    }
    // clang-format on
    len = MIN2(len, MAX_DATA_SIZE);
    uint32_t tot_size = ALIGN_UP((uint32_t)HDR_SIZE + (uint32_t)len, 4);
    uint8_t  pad_len = (uint8_t)(tot_size - (HDR_SIZE + len));

    uint_fast32_t tail = atomic_load_explicit(&log_ring.tail, memory_order_relaxed);
    uint32_t      head;

    int retries = 120;
    // re-evaluate head if we are competing for tail
    while (true) {
        head = atomic_load_explicit(&log_ring.head, memory_order_relaxed);

        if ((head + tot_size - tail) <= log_ring.size) {
            break; // Space is available
        }

        uint32_t new_tail = ALIGN_UP(head + tot_size - log_ring.size, 4);
        if (atomic_compare_exchange_weak(&log_ring.tail, &tail, new_tail)) {
            atomic_fetch_add_explicit(&log_stats.drop_cnt, 1, memory_order_relaxed);
            break; // tail pushed
        }
        // If CAS failed, 'tail' is updated; loop restarts and re-loads 'head'

        // bailout: too much contention
        // clang-format off
        if (--retries <= 0) {
            printf("\r\n%s: " UI_COLOR_RED "[LOG] Error: " UI_STYLE_RESET "Contention Bailout\n", __func__);
            return;
        }
        // clang-format on
    }

    uint32_t start_pos = atomic_fetch_add(&log_ring.head, tot_size);
    uint32_t hid = start_pos & log_ring.mask;

    uint64_t  ts = get_timestamp48();
    log_hdr_t hdr = {
        .tag = LOG_TAG_FREE,
        .lvl = level,
        .dom = domain,
        .ent = entity,
        .pad = pad_len,
        .len = (uint8_t)len,
        .ts_high = (uint16_t)(ts >> 32),
        .ts_low = (uint32_t)(ts & 0xFFFFFFFF)
    };

    if (hid <= (log_ring.size - HDR_SIZE)) {
        *(uint64_t *)(&log_ring.data[hid]) = *(uint64_t *)&hdr;
        *(uint32_t *)(&log_ring.data[hid + 8]) = *(uint32_t *)((uint8_t *)&hdr + 8);
    } else {
        copy_to_ring(start_pos, &hdr, HDR_SIZE);
    }

    copy_to_ring(start_pos + HDR_SIZE, buf, len);
    zero_padding(start_pos + HDR_SIZE + len, pad_len);

    atomic_thread_fence(memory_order_release);
    log_ring.data[hid] = LOG_TAG_SEAL; // seal the message (log entry)
    atomic_store(&log_ring.dirty, true);
    atomic_fetch_add_explicit(&log_stats.sum, 1, memory_order_relaxed);
}

// assuming that caller checked if log_is_dirty()
void log_flash(log_writer_t writer)
{
    uint32_t head = atomic_load_explicit(&log_ring.head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&log_ring.tail, memory_order_relaxed);

    while (tail < head) {
        log_hdr_t hdr;
        uint32_t  tid = tail & log_ring.mask;
        // is new tail produced
        uint32_t pending_tail = atomic_load_explicit(&log_ring.tail, memory_order_relaxed);

        if (pending_tail > tail) {
            tail = pending_tail;
            continue;
        }

        if (log_ring.data[tid] != LOG_TAG_SEAL) {
            uint32_t search_start = tail;

            // scan forward 4 bytes at a time
            while (tail < head && log_ring.data[tail & log_ring.mask] != LOG_TAG_SEAL) {
                tail += 4;
            }

            if (tail < head && log_ring.data[tail & log_ring.mask] == LOG_TAG_SEAL) {
                atomic_store_explicit(&log_ring.tail, tail, memory_order_release);
                tid = tail & log_ring.mask;
                // clang-format off
                printf("\r\n%s: " UI_COLOR_YELLOW "[LOG] Sync Recovery: " UI_STYLE_RESET
                       "skipped %u bytes to find next tag\r\n", __func__, tail - search_start);
                // clang-format on
            } else {
                break;
            }
        }

        if (tid <= (log_ring.size - HDR_SIZE)) {
            *(uint64_t *)&hdr = *(uint64_t *)(&log_ring.data[tid]);
            *(uint32_t *)((uint8_t *)&hdr + 8) = *(uint32_t *)(&log_ring.data[tid + 8]);
        } else {
            copy_from_ring(&hdr, tail, HDR_SIZE);
        }
        // was header evicted while we read it?
        if (atomic_load_explicit(&log_ring.tail, memory_order_relaxed) != tail) {
            tail = atomic_load_explicit(&log_ring.tail, memory_order_relaxed);
            continue;
        }

        uint64_t ts = ((uint64_t)hdr.ts_high << 32) | hdr.ts_low;

        alignas(8) char buf[256];
        copy_from_ring(buf, tail + HDR_SIZE, hdr.len);
        buf[hdr.len] = '\0';

        if (writer)
            writer(hdr.dom, hdr.ent, hdr.lvl, ts, buf, hdr.len);

        // mark the log entry free and update global tail
        log_ring.data[tid] = LOG_TAG_FREE;
        tail += (uint32_t)HDR_SIZE + (uint32_t)hdr.len + (uint32_t)hdr.pad;
        atomic_store_explicit(&log_ring.tail, tail, memory_order_release);
    }

    atomic_store(&log_ring.dirty, false);
}

int log_is_dirty(void)
{
    return atomic_load(&log_ring.dirty);
}

void log_set_level(int did, int eid, int level)
{
    if (did < MAX_DOMAIN && eid < MAX_ENTITY && level <= MAX_LOG_LEVEL) {
        log_level[PACK_ID(did, eid)] = (uint8_t)level;
    }
}

int log_get_level(int did, int eid)
{
    if (did < MAX_DOMAIN && eid < MAX_ENTITY) {
        return log_level[PACK_ID(did, eid)];
    }
    return -1; // Invalid entity + domain
}

void log_msg(const char *fmt, ...)
{
    uint8_t raw = (uint8_t)fmt[0];
    uint8_t level = (raw == 0xff) ? 0 : raw;
    raw = (uint8_t)fmt[1];
    uint8_t domain = (raw == 0xff) ? 0 : raw;
    raw = (uint8_t)fmt[2];
    uint8_t entity = (raw == 0xff) ? 0 : raw;

    const char *efmt = &fmt[4]; // real format

    if (level > log_level[PACK_ID(domain, entity)])
        return;

    va_list args;
    va_start(args, fmt);
    log_dispatch(domain, entity, level, efmt, args);
    va_end(args);
}

void log_get_stats(log_stats_t *stats)
{
    stats->sum = atomic_load_explicit(&log_stats.sum, memory_order_relaxed);
    stats->drop_cnt = atomic_load_explicit(&log_stats.drop_cnt, memory_order_relaxed);
}