#pragma once

void cli_log_cmd_init(void);
int  cli_log_find_name_id(const char *name, int type);

const char *entity_name(uint8_t entity, bool cap);
const char *domain_name(uint8_t domain, bool cap);
const char *level_name(uint8_t level, bool cap);

extern const int      level_id[];
extern const uint16_t ent_map_en; // bitmap of enabled entities
extern const uint16_t dom_map_en; // bitmap of enabled domains
