/**
 * @file log.h
 * @brief Logging interface definitions
 */
#pragma once

typedef struct log_stats_s {
    uint32_t sum;
    uint32_t drop_cnt;
} log_stats_t;

#define LOG_LEVEL_NONE 0 // logging disabled
#define LOG_LEVEL_CRITICAL 10
#define LOG_LEVEL_ERROR 20
#define LOG_LEVEL_WARNING 30
#define LOG_LEVEL_INFO 40
#define LOG_LEVEL_DEBUG 50
#define NUM_LOG_LEVELS 6
#define MAX_LOG_LEVEL LOG_LEVEL_DEBUG

// We don't use 0 ('\0') since it terminates str
#define LOG_MARKER_NONE "\xff"    /* 0 */
#define LOG_MARKER_CRITICAL "\xa" /* 10 */
#define LOG_MARKER_ERROR "\x14"   /* 20 */
#define LOG_MARKER_WARNING "\x1e" /* 30 */
#define LOG_MARKER_INFO "\x28"    /* 40 */
#define LOG_MARKER_DEBUG "\x32"   /* 50 */

/**
 * Log level macros with the given entity id and format string.
 * Use the LOG_ENTITY_* macros to create entity-specific log macros with predefined entity IDs.
 * The entity IDs are defined in log_marker.h using the DEV, SYS, or TEST domains.
 * For example, to log an error message for the GPIO device, you would define a macro like:
 * #define LOG_GPIO_ERROR(...) LOG_ENTITY_ERROR(ID_DEV(ENT_GPIO), __VA_ARGS__)
 * and then call LOG_GPIO_ERROR("Failed to initialize GPIO pin %d", pin);
 * while for the test domain, you would define a macro like:
 * #define LOG_GPIO_TEST_ERROR(...) LOG_ENTITY_ERROR(ID_TEST(ENT_GPIO), __VA_ARGS__)
 */
#define LOG_ENTITY_CRITICAL(eid, fmt, ...) log_msg(LOG_MARKER_CRITICAL eid LOG_MARKER_NONE fmt, ##__VA_ARGS__)
#define LOG_ENTITY_ERROR(eid, fmt, ...) log_msg(LOG_MARKER_ERROR eid LOG_MARKER_NONE fmt, ##__VA_ARGS__)
#define LOG_ENTITY_WARNING(eid, fmt, ...) log_msg(LOG_MARKER_WARNING eid LOG_MARKER_NONE fmt, ##__VA_ARGS__)
#define LOG_ENTITY_INFO(eid, fmt, ...) log_msg(LOG_MARKER_INFO eid LOG_MARKER_NONE fmt, ##__VA_ARGS__)
#define LOG_ENTITY_DEBUG(eid, fmt, ...) log_msg(LOG_MARKER_DEBUG eid LOG_MARKER_NONE fmt, ##__VA_ARGS__)

typedef void (*log_writer_t)(uint8_t domain, uint8_t entity, uint8_t level, uint64_t ts, const char *msg, uint16_t len);

void log_set_level(int did, int eid, int level);
int  log_get_level(int did, int eid);
int  log_is_dirty(void);
void log_flash(log_writer_t writer);
// The log_msg function is the main entry point for logging messages. It takes a format string and variable arguments,
// similar to printf. Don't call this function directly. Instead, use the LOG_ENTITY_* macros defined above, which will
// automatically include the appropriate log marker and entity ID.
void log_msg(const char *fmt, ...);
void log_get_stats(log_stats_t *stats);
