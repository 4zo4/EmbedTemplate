#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "profiler_target.h"
#include "profiler.h"
#include "profiler_priv.h"
#include "utils.h"

#define PROF_STOP UINT32_MAX

uint64_t get_timestamp48(void); // prototype without include file

atomic_uint_fast32_t next_sample = 0;
alignas(8) prof_sample_t prof_sample_pool[MAX_PROF_SAMPLES];

static void *func_list[] = PROF_FUNC_LIST;

#define NUM_FUNCTIONS (sizeof(func_list) / sizeof(func_list[0]))
#define MAP_SIZE POW2(NUM_FUNCTIONS * 4)
#define MAP_MASK (MAP_SIZE - 1)
#define BITMAP_UNIT_SIZE 32
#define BITMAP_SHIFT 5
#define BITMAP_UNIT_MASK (BITMAP_UNIT_SIZE - 1)
#define BITMAP_ARRAY_SIZE (((MAP_SIZE) + BITMAP_UNIT_MASK) >> BITMAP_SHIFT)
#define IS_IN_USE(map, idx) ((map)[(idx) >> BITMAP_SHIFT] & (1UL << ((idx) & BITMAP_UNIT_MASK)))
#define SET_IN_USE(map, idx) ((map)[(idx) >> BITMAP_SHIFT] |= (1UL << ((idx) & BITMAP_UNIT_MASK)))

static_assert(NUM_FUNCTIONS <= MAX_PROF_FUNCTIONS, "Maximum number of functions exceeded");

alignas(8) size_t prof_seed; // Perfect hash seed for hashing function addresses
alignas(8) prof_node_t prof_table[NUM_FUNCTIONS];
static uint8_t  prof_lookaside_map[MAP_SIZE];
static uint32_t prof_inuse_map[BITMAP_ARRAY_SIZE];

// Murmur3 Avalanche Bit-Mixer hash function
static inline size_t hash(void *func_addr, size_t seed)
{
    uintptr_t h = (uintptr_t)func_addr ^ (uintptr_t)seed;

#if UINTPTR_MAX == UINT64_MAX
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
#else // 32-bit version
    h ^= h >> 16;
    h *= 0x85ebca6bU;
    h ^= h >> 13;
    h *= 0xc2b2ae35U;
    h ^= h >> 16;
#endif
    return (size_t)(h & MAP_MASK);
}

static inline int get_func_node_index(void *func_addr)
{
    size_t slot = hash(func_addr, prof_seed);

    if (IS_IN_USE(prof_inuse_map, slot)) {
        int tbl_idx = prof_lookaside_map[slot];
        if (prof_table[tbl_idx].func_addr == func_addr) {
            return tbl_idx;
        }
    }
    return -1;
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *func_addr, void *call_site)
{
    (void)call_site; // Unused parameter

    int tbl_idx = get_func_node_index(func_addr);
    if (tbl_idx == -1)
        return; // Ignore functions we aren't tracking

    uint32_t probe = atomic_load_explicit(&next_sample, memory_order_relaxed);
    if (probe == PROF_STOP)
        return;

    uint32_t pool_idx = atomic_fetch_add_explicit(&next_sample, 1, memory_order_relaxed);
    if (pool_idx >= MAX_PROF_SAMPLES) {
        if (pool_idx == MAX_PROF_SAMPLES) {
            atomic_store_explicit(&next_sample, PROF_STOP, memory_order_relaxed);
        }
        return;
    }

    prof_sample_t *sample = &prof_sample_pool[pool_idx];
    atomic_store_explicit(&sample->entry.ll, PROF_NULL_INDEX, memory_order_relaxed);
    sample->entry.ts = get_timestamp48();

    prof_node_t *func_node = &prof_table[tbl_idx];
    uint32_t     tail = atomic_load_explicit(&func_node->tail, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(
        &func_node->tail, &tail, pool_idx,
        memory_order_release, memory_order_relaxed
    )) {
    }

    if (tail != PROF_NULL_INDEX) {
        atomic_store_explicit(&prof_sample_pool[tail].entry.ll, pool_idx, memory_order_release);
    } else {
        atomic_store_explicit(&func_node->head, pool_idx, memory_order_release);
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void *func_addr, void *call_site)
{
    (void)call_site; // Unused parameter

    int tbl_idx = get_func_node_index(func_addr);
    if (tbl_idx == -1)
        return; // Ignore functions we aren't tracking

    uint32_t probe = atomic_load_explicit(&next_sample, memory_order_relaxed);
    if (probe == PROF_STOP)
        return;

    prof_node_t *func_node = &prof_table[tbl_idx];
    uint32_t     pool_idx = atomic_load_explicit(&func_node->tail, memory_order_acquire);
    if (pool_idx == PROF_NULL_INDEX)
        return; // No entry recorded for this function, possibly due to overflow

    prof_sample_t *sample = &prof_sample_pool[pool_idx];
    sample->exit.ts = get_timestamp48();
}

static uint32_t prof_stop(void)
{
    uint32_t pool_idx = atomic_exchange_explicit(&next_sample, PROF_STOP, memory_order_seq_cst);

    if (pool_idx >= MAX_PROF_SAMPLES)
        return MAX_PROF_SAMPLES;

    return pool_idx;
}

void prof_start(uint32_t dirty_samples)
{
    if (atomic_load_explicit(&next_sample, memory_order_relaxed) == PROF_STOP) {
        for (int i = 0; i < NUM_FUNCTIONS; i++) {
            atomic_store_explicit(&prof_table[i].head, PROF_NULL_INDEX, memory_order_relaxed);
            atomic_store_explicit(&prof_table[i].tail, PROF_NULL_INDEX, memory_order_relaxed);
        }

        if (dirty_samples > 0 && dirty_samples < MAX_PROF_SAMPLES) {
            memset(prof_sample_pool, 0, sizeof(prof_sample_pool[0]) * dirty_samples);
        } else {
            memset(prof_sample_pool, 0, sizeof(prof_sample_pool));
        }
        atomic_store_explicit(&next_sample, 0, memory_order_seq_cst);
    }
}

void prof_get_stats(prof_stats_t *buf, int *count)
{
    uint32_t total_samples = prof_stop();
    if (total_samples == 0) {
        *count = 0;
        return;
    }

    int stats_count = 0;
    int max_stats = MIN2(*count, NUM_FUNCTIONS);

    for (int i = 0; i < max_stats; i++) {
        prof_node_t *node = &prof_table[i];

        uint32_t sample_idx = *(uint32_t *)&node->head;
        if (sample_idx == PROF_NULL_INDEX)
            continue;

        uint64_t total_duration = 0;
        uint64_t max_duration = 0;
        uint64_t min_duration = UINT64_MAX;
        uint32_t sample_count = 0;

        uint64_t ts_first = 0;
        uint64_t ts_last = 0;

        while (sample_idx != PROF_NULL_INDEX && sample_idx < total_samples) {
            prof_sample_t *sample = &prof_sample_pool[sample_idx];

            if (sample->entry.ts != 0 && sample->exit.ts != 0 && sample->exit.ts >= sample->entry.ts) {
                uint64_t duration = sample->exit.ts - sample->entry.ts;

                total_duration += duration;
                if (duration > max_duration)
                    max_duration = duration;
                if (duration < min_duration)
                    min_duration = duration;
                if (sample_count == 0)
                    ts_first = sample->entry.ts;
                ts_last = sample->exit.ts;
                sample_count++;
            }

            sample_idx = *(uint32_t *)&sample->entry.ll;
        }

        if (sample_count > 0) {
            buf[stats_count].func_addr = (uintptr_t)node->func_addr;
            buf[stats_count].min_duration = (uint32_t)min_duration;
            buf[stats_count].max_duration = (uint32_t)max_duration;
            buf[stats_count].avg_duration = (uint32_t)(total_duration / sample_count);
            buf[stats_count].sample_count = sample_count;
            buf[stats_count].ts_start = ts_first;
            buf[stats_count].ts_end = ts_last;
            stats_count++;
        }
    }

    *count = stats_count;

    prof_start(total_samples);
}

// Initialize the profiler's hash table and perfect seed for function address hashing

static int try_seed(size_t seed)
{
    bool slot_taken[MAP_SIZE] = {false};
    for (size_t i = 0; i < NUM_FUNCTIONS; i++) {
        size_t slot = hash(func_list[i], seed);
        if (slot_taken[slot])
            return -1;
        slot_taken[slot] = true;
    }
    return (int)seed;
}

static size_t find_perfect_seed()
{
    for (size_t seed = 0; seed < 10000; seed++) {
        if (try_seed(seed) != -1)
            return seed;
    }
    return (size_t)-1; // No perfect seed found
}

void init_profiler(void)
{
    prof_seed = find_perfect_seed();
    assert(prof_seed != (size_t)-1 && "Failed to find perfect seed");

    memset(prof_sample_pool, 0, sizeof(prof_sample_pool));

    for (int i = 0; i < BITMAP_ARRAY_SIZE; i++) {
        prof_inuse_map[i] = 0;
    }

    for (int i = 0; i < NUM_FUNCTIONS; i++) {
        size_t slot = hash(func_list[i], prof_seed);
        prof_lookaside_map[slot] = (uint8_t)i;
        SET_IN_USE(prof_inuse_map, slot);
        prof_table[i].func_addr = func_list[i];
        atomic_init(&prof_table[i].head, PROF_NULL_INDEX);
        atomic_init(&prof_table[i].tail, PROF_NULL_INDEX);
    }
}
