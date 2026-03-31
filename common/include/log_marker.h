/**
 * @file log_marker.h
 * @brief Log marker definitions for log macros in log.h
 */
#pragma once

// Domain Markers for string concatenation
#define DOM_NONE "\xff" // we don't use '\0' since it terminates str
#define DOM_DEV "\x01"
#define DOM_SYS "\x02"
#define DOM_TEST "\x03"

// Domain IDs
typedef enum {
    DOMAIN_NONE,
    DOMAIN_DEV,  // Device
    DOMAIN_SYS,  // System
    DOMAIN_TEST, // Test
    MAX_DOMAIN
} domain_e;

#define NUM_DOMAINS MAX_DOMAIN

// Entity definitions can be added here.
// Follow the same pattern of defining a unique marker for string concatenation and
// an associated ID for log filtering.

// Entity Markers for string concatenation
#define ENT_NONE "\xff" // we don't use '\0' since it terminates str
#define ENT_SIM "\x01"  // Simulation (for system-level logs from the hardware simulator)
#define ENT_CLI "\x02"
#define ENT_LOG "\x03"
#define ENT_GPIO "\x04"
#define ENT_SYSCTRL "\x05"
#define ENT_TIMER "\x06"
#define ENT_UART "\x07"

// Entity IDs
typedef enum {
    ENTITY_NONE,
    ENTITY_SIM,
    ENTITY_CLI,
    ENTITY_LOG,
    ENTITY_GPIO,
    ENTITY_SYSCTRL,
    ENTITY_TIMER,
    ENTITY_UART,
    MAX_ENTITY
} entity_e;

#define NUM_ENTITIES MAX_ENTITY

typedef enum {
    CLI = 0,
    LOG,
    MAX_INFRA_INDEX
} Infra_e;

#define NUM_SYS_BLOCKS MAX_INFRA_INDEX

// clang-format off
#define BITS_REQUIRED(n) \
    ((n) < 2   ? 1 : \
     (n) < 4   ? 2 : \
     (n) < 8   ? 3 : \
     (n) < 16  ? 4 : \
     (n) < 32  ? 5 : \
     (n) < 64  ? 6 : \
     (n) < 128 ? 7 : \
     (n) < 256 ? 8 : \
                 16)
// clang-format on
#define ENTITY_BITS BITS_REQUIRED(MAX_ENTITY - 1)
#define DOMAIN_BITS BITS_REQUIRED(MAX_DOMAIN - 1)

#define ENTITY_MASK ((1 << ENTITY_BITS) - 1)
#define DOMAIN_MASK ((1 << DOMAIN_BITS) - 1)

#define PACK_ID(dom, ent) ((((uint16_t)(dom) & DOMAIN_MASK) << ENTITY_BITS) | ((uint8_t)(ent) & ENTITY_MASK))

#define LOG_LEVEL_SIZE (1 << (ENTITY_BITS + DOMAIN_BITS))

typedef enum {
    TYPE_NONE,
    TYPE_DOMAIN,
    TYPE_ENTITY,
    TYPE_LOG_LEVEL
} type_e;

// Identity Macros
// These macros are used to create the full entity ID by combining the domain and entity markers.
// For example, ID_DEV(ENT_GPIO) will produce a unique identifier for the GPIO device

// Global (non-entity specific)
#define ID_G_DEV DOM_DEV ENT_NONE
#define ID_G_SYS DOM_SYS ENT_NONE
#define ID_G_TEST DOM_TEST ENT_NONE
// Entity specific
#define ID_DEV(ent) DOM_DEV ent
#define ID_SYS(ent) DOM_SYS ent
#define ID_TEST(ent) DOM_TEST ent
