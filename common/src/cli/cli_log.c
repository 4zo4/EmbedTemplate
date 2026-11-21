#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cli_log.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h" // for BIT

// for name to id matching e.g., "CRITICAL" to LOG_LEVEL_CRITICAL (10)
typedef struct name_id_s {
    uint32_t hash;
    int      id;
} name_id_t;

// sentinel value to indicate an empty slot in the map
#define MAP_EMPTY 0xFF

#define MAP_ALL_ENTITIES ((1U << MAX_ENTITY) - 1)
#define MAP_ALL_DOMAINS ((1U << MAX_DOMAIN) - 1)

#define DOMAIN_MAP_SIZE POW2(NUM_DOMAINS * 2)
#define ENTITY_MAP_SIZE POW2(NUM_ENTITIES * 2)
#define LOG_LEVEL_MAP_SIZE POW2(NUM_LOG_LEVELS * 2)

#define DOMAIN_MAP_MASK (DOMAIN_MAP_SIZE - 1)
#define ENTITY_MAP_MASK (ENTITY_MAP_SIZE - 1)
#define LOG_LEVEL_MAP_MASK (LOG_LEVEL_MAP_SIZE - 1)

static_assert((DOMAIN_MAP_SIZE & DOMAIN_MAP_MASK) == 0, "DOMAIN_MAP_SIZE must be power of 2");
static_assert((ENTITY_MAP_SIZE & ENTITY_MAP_MASK) == 0, "ENTITY_MAP_SIZE must be power of 2");
static_assert((LOG_LEVEL_MAP_SIZE & LOG_LEVEL_MAP_MASK) == 0, "LOG_LEVEL_MAP_SIZE must be power of 2");

// -- Globals --

static uint8_t   domain_map[DOMAIN_MAP_SIZE];
static name_id_t domain_tbl[NUM_DOMAINS - 1]; // exclude DOMAIN_NONE
static uint8_t   entity_map[ENTITY_MAP_SIZE];
static name_id_t entity_tbl[NUM_ENTITIES];
static uint8_t   level_map[LOG_LEVEL_MAP_SIZE];
static name_id_t level_tbl[NUM_LOG_LEVELS];

const int level_id[NUM_LOG_LEVELS] = {LOG_LEVEL_NONE, LOG_LEVEL_CRITICAL, LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

#ifdef ENABLE_SIM
#define ENT_MASK_SIM 0U
#else
#define ENT_MASK_SIM BIT(ENTITY_SIM)
#endif

#ifdef ENABLE_BLE
#define ENT_MASK_BLE 0U
#else
#define ENT_MASK_BLE BIT(ENTITY_BLE)
#endif

#ifdef ENABLE_PCI
#define ENT_MASK_PCI 0U
#else
#define ENT_MASK_PCI BIT(ENTITY_PCI)
#endif

const uint16_t ent_map_en = (MAP_ALL_ENTITIES & ~(ENT_MASK_SIM | ENT_MASK_BLE | ENT_MASK_PCI));

#ifdef ENABLE_TEST
const uint16_t dom_map_en = MAP_ALL_DOMAINS;
#else
const uint16_t dom_map_en = (MAP_ALL_DOMAINS & ~(1U << DOMAIN_TEST));
#endif

// -- End of globals --

static uint32_t cliHash(const char *str)
{
    uint32_t hash = 5381;
    uint32_t c;

    while ((c = (uint8_t)*str++)) {
        if (c >= 'a' && c <= 'z') {
            c -= 32; // convert to all capital
        }
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static void do_insert(uint8_t *map, uint32_t map_mask, name_id_t *table, int idx)
{
    uint32_t hash = table[idx].hash;
    uint32_t slot = hash & map_mask;

    while (map[slot] != MAP_EMPTY) {
        if (table[map[slot]].hash == hash)
            break;
        slot = (slot + 1) & map_mask;
    }
    map[slot] = (uint8_t)idx;
}

static bool insert_name(const char *name, int id, int type, int n)
{
    uint32_t hash = cliHash(name);

    switch (type) {
    case TYPE_DOMAIN: {
        domain_tbl[n].hash = hash;
        domain_tbl[n].id = id;
        do_insert(domain_map, DOMAIN_MAP_MASK, domain_tbl, n);
        return true;
    }
    case TYPE_ENTITY: {
        entity_tbl[n].hash = hash;
        entity_tbl[n].id = id;
        do_insert(entity_map, ENTITY_MAP_MASK, entity_tbl, n);
        return true;
    }
    case TYPE_LOG_LEVEL: {
        level_tbl[n].hash = hash;
        level_tbl[n].id = id;
        do_insert(level_map, LOG_LEVEL_MAP_MASK, level_tbl, n);
        return true;
    }
    }
    return false;
}

int cli_log_find_name_id(const char *name, int type)
{
    name_id_t *table;
    uint8_t   *map;
    uint32_t   mask;
    uint32_t   hash = cliHash(name);

    if (type == TYPE_DOMAIN) {
        map = domain_map;
        table = domain_tbl;
        mask = DOMAIN_MAP_MASK;
    } else if (type == TYPE_ENTITY) {
        map = entity_map;
        table = entity_tbl;
        mask = ENTITY_MAP_MASK;
    } else if (type == TYPE_LOG_LEVEL) {
        map = level_map;
        table = level_tbl;
        mask = LOG_LEVEL_MAP_MASK;
    } else {
        return -1;
    }

    uint32_t slot = hash & mask;
    uint32_t start = slot;

    while (map[slot] != MAP_EMPTY) {
        uint8_t idx = map[slot];
        if (table[idx].hash == hash)
            return table[idx].id;

        slot = (slot + 1) & mask;
        if (slot == start)
            break;
    }

    return -1;
}

void cli_log_cmd_init(void)
{
    const char *name = nullptr;

    memset(domain_map, MAP_EMPTY, sizeof(domain_map));
    memset(entity_map, MAP_EMPTY, sizeof(entity_map));
    memset(level_map, MAP_EMPTY, sizeof(level_map));

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        name = domain_name((uint8_t)did, 1);
        insert_name(name, did, TYPE_DOMAIN, did - 1);
    }

    for (int eid = 0; eid < NUM_ENTITIES; eid++) {
        if (!(ent_map_en & BIT(eid)))
            continue;
        name = entity_name((uint8_t)eid, 1);
        insert_name(name, eid, TYPE_ENTITY, eid);
    }

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        name = level_name((uint8_t)level_id[i], 1);
        insert_name(name, level_id[i], TYPE_LOG_LEVEL, i);
    }
}
