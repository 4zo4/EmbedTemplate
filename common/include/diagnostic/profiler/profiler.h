#pragma once

typedef struct prof_stats_s {
    uint64_t  ts_start;
    uint64_t  ts_end;
    uintptr_t func_addr;
    uint32_t  min_duration;
    uint32_t  max_duration;
    uint32_t  avg_duration;
    uint32_t  sample_count;
} __attribute__((aligned(8))) prof_stats_t;

void init_profiler(void);                           // Initialize the profiler and set up the function address hash table
void prof_start(uint32_t dirty_samples);            // Start profiling by resetting the sample pool and linked lists
void prof_get_stats(prof_stats_t *buf, int *count); // Retrieve profiler statistics for profiled functions, including min, max, and average durations
