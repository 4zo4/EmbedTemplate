#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_log.h"
#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "pack.h"  // for stream_t
#include "utils.h" // for BIT

int set_sim_cfg(int len, stream_t *cfg);

// set command implementation

static void do_log_help(EmbeddedCli *cli, const char **matches, int count, uint8_t pos)
{
    int         n = 5; // initial size of msg
    char        msg[128] = "use: ";
    const char *name;

    if (matches) {
        for (int i = 0; i < count; i++) {
            n += snprintf(msg + n, sizeof(msg) - n, "%s", matches[i]);
            msg[n++] = '|';
        }
        goto done;
    }

    if (pos == 2 || pos == 3)
        n += snprintf(msg + n, sizeof(msg) - n, "all|");

    if (pos == 2) {
        for (int i = 1; i < NUM_DOMAINS; i++) {
            if (!(dom_map_en & BIT(i)))
                continue;
            n += snprintf(msg + n, sizeof(msg) - n, "%s", domain_name((uint8_t)i, 0));
            msg[n++] = '|';
        }
    } else if (pos == 3) {
        for (int i = 0; i < NUM_ENTITIES; i++) {
            if (!(ent_map_en & BIT(i)))
                continue;
            n += snprintf(msg + n, sizeof(msg) - n, "%s", entity_name((uint8_t)i, 0));
            msg[n++] = '|';
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            n += snprintf(msg + n, sizeof(msg) - n, "%s", level_name((uint8_t)level_id[i], 0));
            msg[n++] = '|';
        }
    } else {
        return;
    }
done:
    msg[n - 1] = '\0';
    embeddedCliPrint(cli, msg);
}

bool set_log_validate(const char *token, uint8_t pos)
{
    if (pos == 2 || pos == 3) {
        if (strcmp(token, "all") == 0)
            return true;
    }

    if (pos == 2) {
        for (int did = 1; did < NUM_DOMAINS; did++) {
            if ((dom_map_en & BIT(did)) && strcmp(token, domain_name(did, 0)) == 0)
                return true;
        }
    } else if (pos == 3) {
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if ((ent_map_en & BIT(eid)) && strcmp(token, entity_name(eid, 0)) == 0)
                return true;
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            if (strcmp(token, level_name((uint8_t)level_id[i], 0)) == 0)
                return true;
        }
    }
    return false;
}

void set_log_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    const char *match = nullptr;
    const char *matches[8];
    size_t      len = strlen(token);
    int         count = 0;

    if (len == 0) {
        do_log_help(cli, nullptr, 0, pos);
        return;
    }

    bool help = (strchr(token, '?') != nullptr);
    if (help) {
        do_log_help(cli, nullptr, 0, pos);
        return;
    }

    if (pos == 2 || pos == 3) {
        if (strncmp(token, "all", len) == 0) {
            embeddedCliCompletion(cli, "all");
            return;
        }
    }

    if (pos == 2) {
        for (int did = 1; did < NUM_DOMAINS; did++) {
            if (!(dom_map_en & BIT(did)))
                continue;
            if (count == 8)
                break;
            match = domain_name((uint8_t)did, 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    } else if (pos == 3) {
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if (!(ent_map_en & BIT(eid)))
                continue;
            if (count == 8)
                break;
            match = entity_name(eid, 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            if (count == 8)
                break;
            match = level_name((uint8_t)level_id[i], 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    }

    if (count == 1)
        embeddedCliCompletion(cli, matches[0]);
    else if (count > 1)
        do_log_help(cli, matches, count, pos);
}

void on_set_log_cmd(EmbeddedCli *cli, char *args, int count)
{
    const char *dom = embeddedCliGetToken(args, 2);
    const char *ent = embeddedCliGetToken(args, 3);
    const char *lvl = embeddedCliGetToken(args, 4);

    int level = cli_log_find_name_id(lvl, TYPE_LOG_LEVEL);
    if (level == -1) {
        embeddedCliPrint(cli, "[ERROR] Invalid log level");
        return;
    }

    bool all_domains = (strcmp(dom, "all") == 0);
    bool all_entities = (strcmp(ent, "all") == 0);

    int did = all_domains ? 0 : cli_log_find_name_id(dom, TYPE_DOMAIN);
    if (!all_domains && did == -1) {
        embeddedCliPrint(cli, "[ERROR] Unknown log domain");
        return;
    }
    int eid = all_entities ? 0 : cli_log_find_name_id(ent, TYPE_ENTITY);
    if (!all_entities && eid == -1) {
        embeddedCliPrint(cli, "[ERROR] Unknown log entity");
        return;
    }

    for (int d = 1; d < NUM_DOMAINS; d++) {
        if (!all_domains && d != did)
            continue;
        for (int e = 0; e < NUM_ENTITIES; e++) {
            if (!all_entities && e != eid)
                continue;
            log_set_level(d, e, level);
        }
    }
    embeddedCliPrint(cli, "Log level set");
}

#ifdef ENABLE_SIM
static void sim_cfg_print_oor(EmbeddedCli *cli, int ret)
{
    static const char *sim_cfg_names[] = {
        "", "Ramp Time", "Thermal Mass", "Conductance", "Heater Power", "", "", "",                             // bits 0-7 (PHY)
        "", "Sensor latency", "Target Temp", "Critical Temp", "Cooldown Temp", "Hysteresis", "Ambient Temp", "" // bits 8-15 (TEMP)
    };

    char      msg[192] = "[SKIP] Out of Range: ";
    int       n = 21;
    uint16_t  oor_mask = ret;
    const int last_bit = HIGHEST_BIT(oor_mask);

    for (int i = 1; i <= last_bit; i++) {
        if (!(oor_mask & (1U << i)) || sim_cfg_names[i][0] == '\0')
            continue;
        int m = snprintf(msg + n, sizeof(msg) - n, "%s, ", sim_cfg_names[i]);
        if (m > 0)
            n += m;
        if (n >= (int)sizeof(msg) - 1)
            break;
    }

    if (n > 23) {
        msg[n - 2] = '\0';
        embeddedCliPrint(cli, msg);
    }
}

void on_set_sim_cmd(EmbeddedCli *cli, char *args, int count)
{
    const char *arg = embeddedCliGetToken(args, 2); // Guaranteed to be "phy" or "temp"
    bool        temp = (strcmp(arg, "temp") == 0);
    int         ret = 0;
    uint16_t    oor_mask = 0;
    uint16_t    cfg_mask = 0;
    uint8_t     hdr = 0;
    uint16_t    val;
    stream_t    pkt[2] = {0};

    if (temp) {
        // tokens 3 to 7 map to the 5 temp args
        for (int i = 0; i < 5 && (i + 3 <= count); i++) {
            arg = embeddedCliGetToken(args, i + 3);
            val = (uint16_t)atoi(arg);
            if (val < 256) {
                pkt[1].u8[i + 1] = (uint8_t)val;
                hdr |= BIT(i + 1);
            } else {
                oor_mask |= BIT2(i + 1);
            }
        }
        cfg_mask = (uint16_t)hdr << 8;
        pkt[0].u8[0] = BIT(0); // Continue bit
        pkt[1].u8[0] = hdr;
        ret = set_sim_cfg(2, pkt) | oor_mask;
    } else {
        arg = embeddedCliGetToken(args, 3);
        val = (uint16_t)atoi(arg);
        if (val < 256) {
            pkt[0].u8[1] = (uint8_t)val;
            hdr |= BIT(1);
        } else {
            oor_mask |= BIT(1);
        }

        for (int i = 1; i < 5 && (i + 3 <= count); i++) {
            arg = embeddedCliGetToken(args, i + 3);
            val = (uint16_t)atoi(arg);
            if (val) {
                pkt[0].u16[i] = val;
                hdr |= BIT(i + 1);
            }
        }

        cfg_mask = (uint16_t)hdr;
        pkt[0].u8[0] = hdr;

        // Special handling for the 5th PHY arg (ambient) if count == 7
        if (count == 7) {
            arg = embeddedCliGetToken(args, 7);
            val = (uint16_t)atoi(arg);
            pkt[1].u16[3] = val;
            pkt[0].u8[0] |= BIT(0); // mark continue
            pkt[1].u8[0] = BIT(6);  // mark ambient field present
            cfg_mask |= BIT2(6);
            ret = set_sim_cfg(2, pkt);
        } else {
            ret = set_sim_cfg(1, pkt);
        }
    }

    if (ret > 0)
        sim_cfg_print_oor(cli, ret);
    if ((cfg_mask & ~(uint16_t)ret) != 0)
        embeddedCliPrint(cli, "Sim config set");
}
#else // !SIM
void on_set_sim_cmd(EmbeddedCli *cli, char *args, int count)
{
}
#endif
