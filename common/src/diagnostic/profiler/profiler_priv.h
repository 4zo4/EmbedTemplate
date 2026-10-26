/*
 * @file profiler_priv.h
 * @brief This file defines the internal structures and constants used by the profiler.
 * It includes definitions for profiling nodes, entries, and samples.
 */
#pragma once

#define MAX_PROF_SAMPLES 1024
#define MAX_PROF_FUNCTIONS 255
#define PROF_NULL_INDEX 0xFFFFFFFF

#ifdef __cplusplus
#define ATOMIC_UINT_FAST32 std::atomic<std::uint_fast32_t>
#else
#define ATOMIC_UINT_FAST32 atomic_uint_fast32_t
#endif

static_assert((sizeof(ATOMIC_UINT_FAST32) == 4), "ATOMIC_UINT_FAST32 is not 4-byte aligned");

typedef struct prof_node_s {
    void *func_addr; // Address of profiled function
#if UINTPTR_MAX == UINT32_MAX
    uint32_t pad; // Padding for 8-byte alignment
#endif
    ATOMIC_UINT_FAST32 head; // Linked list head index
    ATOMIC_UINT_FAST32 tail; // Linked list tail index
} __attribute__((aligned(8))) prof_node_t;

static_assert((sizeof(prof_node_t) % 8) == 0, "prof_node_t is not 8-byte aligned");

typedef struct prof_entry_s {
    uint64_t           ts;  // Timestamp of the entry
    ATOMIC_UINT_FAST32 ll;  // Linked list index
    uint32_t           pad; // Padding for 8-byte alignment
} __attribute__((aligned(8))) prof_entry_t;

static_assert((sizeof(prof_entry_t) % 8) == 0, "prof_entry_t is not 8-byte aligned");

typedef struct prof_sample_s {
    prof_entry_t entry; // Sample entry
    prof_entry_t exit;  // Sample exit
} __attribute__((aligned(8))) prof_sample_t;

static_assert((sizeof(prof_sample_t) % 8) == 0, "prof_sample_t is not 8-byte aligned");

#ifdef __cplusplus
extern "C" {
#endif

extern prof_node_t prof_table[]; // Profiled functions hash lookup table

#ifdef __cplusplus
}
#endif
