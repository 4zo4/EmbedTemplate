/*
 * profiler_target.h
 * @brief This file defines the list of functions to be profiled.
 * In this sample case, it includes profiler target-specific function list for tracing the logging infrastructure.
 * The user should modify this list to include functions of interest for specific profiling.
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>

typedef void (*log_writer_t)(uint8_t domain, uint8_t entity, uint8_t level, uint64_t ts, const char *msg, uint16_t len);
void log_dispatch(uint8_t domain, uint8_t entity, uint8_t level, const char *fmt, va_list args);
void log_flash(log_writer_t writer);

#define PROF_FUNC_LIST {log_dispatch, log_flash}
