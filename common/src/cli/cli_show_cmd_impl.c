#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_log.h"
#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "pack.h" // for stream_t
#include "profiler.h"
#include "utils.h" // for BIT

int get_sim_cfg(int len, stream_t *cfg, int what);

// show command implementation

void show_stats_sys(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "System Statistics ...");
}

void show_stats_log(EmbeddedCli *cli, char *args, int count)
{
    log_stats_t stats;
    log_get_stats(&stats);
    char msg[64];
    snprintf(msg, sizeof(msg), "Log Statistics: total logs %lu, dropped %u", stats.sum, stats.drop_cnt);
    embeddedCliPrint(cli, msg);
}

#ifdef ENABLE_PROF
static void fmt_timestamp(char *buf, size_t buf_sz, uint64_t ts)
{
    uint64_t total_us = ts % 1000000ULL;
    uint64_t total_sec = ts / 1000000ULL;

    uint32_t us = (uint32_t)total_us;
    uint32_t sec = (uint32_t)(total_sec % 60ULL);
    uint32_t min = (uint32_t)((total_sec / 60ULL) % 60ULL);
    uint32_t hour = (uint32_t)((total_sec / 3600ULL) % 24ULL);
    uint32_t days = (uint32_t)(total_sec / 86400ULL);

    if (days > 0) {
        snprintf(buf, buf_sz, "%03ud:%02uh:%02um:%02u.%06us", days, hour, min, sec, us);
    } else if (hour > 0) {
        snprintf(buf, buf_sz, "%02uh:%02um:%02u.%06us", hour, min, sec, us);
    } else if (min > 0) {
        snprintf(buf, buf_sz, "%02um:%02u.%06us", min, sec, us);
    } else {
        snprintf(buf, buf_sz, "%02u.%06us", sec, us);
    }
}
void show_stats_prof(EmbeddedCli *cli, char *args, int count)
{
    alignas(8) static prof_stats_t stats[16];

    int batch_count = (int)(sizeof(stats) / sizeof(stats[0]));
    prof_get_stats(stats, &batch_count);

    for (int i = 0; i < batch_count; i++) {
        char msg[256];
        char ts_start[32], ts_end[32];
        fmt_timestamp(ts_start, sizeof(ts_start), stats[i].ts_start);
        fmt_timestamp(ts_end, sizeof(ts_end), stats[i].ts_end);
        // clang-format off
        snprintf(msg, sizeof(msg), "Func %p: min %u, max %u, avg %u us, %u samples, start %s, end %s",
            (void *)stats[i].func_addr, stats[i].min_duration, stats[i].max_duration,
            stats[i].avg_duration, stats[i].sample_count, ts_start, ts_end);
        // clang-format on
        embeddedCliPrint(cli, msg);
    }
}
#endif

void show_stats_task(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Task Statistics: ...");
}

void show_version(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Version ...");
}

void show_config(EmbeddedCli *cli, char *args, int count)
{
    char               msg[128] = "Config:\r\n";
    uint16_t           bindings_count = embeddedCliGetBindingsCount(cli);
    uint16_t           bindings_size = bindings_count * sizeof(CliCommandBinding);
    EmbeddedCliConfig *config = embeddedCliGetConfig();
    uint16_t           allocated_size = embeddedCliRequiredSize(config);
    uint16_t           max_bindings_count = config->maxBindingsCount;
    config->maxBindingsCount = bindings_count;
    uint16_t required_size = embeddedCliRequiredSize(config);
    config->maxBindingsCount = max_bindings_count;
    // clang-format off
    snprintf(msg + 9, sizeof(msg) - 9, " CLI binding count %u (%u bytes) CLI buffer required %u allocated %u bytes",
            bindings_count, bindings_size, required_size, allocated_size);
    // clang-format on
    embeddedCliPrint(cli, msg);
}

void show_config_sim(EmbeddedCli *cli, char *args, int count)
{
#ifdef ENABLE_SIM
    const char *opt = embeddedCliGetToken(args, 3);
    int         what = 0; // 'current'

    if (strcmp(opt, "default") == 0)
        what = 1;
    else if (strcmp(opt, "ranges") == 0)
        what = 2;

    alignas(8) char msg[256 + 8] = "Simulation config:";

    if (what < 2) {
        stream_t pkt[2];
        get_sim_cfg(2, pkt, what);

        // clang-format off
        snprintf(msg + 18, sizeof(msg) - 18,
            "\r\n Thermal Mass: %u J/°C, Conductance: %u mW/°C, Heater Power: %u mW"
            "\r\n Ambient Temp: %d°C, Ramp Time: %u sec, Sensor latency: %u ms"
            "\r\n Target Temp: %u°C, Critical Temp: %u°C"
            "\r\n Cooldown Temp: %u°C, Hysteresis: %u°C",
            pkt[0].u16[1], pkt[0].u16[2], pkt[0].u16[3],
            (int16_t)pkt[1].u16[3], pkt[0].u8[1], pkt[1].u8[1],
            pkt[1].u8[2], pkt[1].u8[3],
            pkt[1].u8[4], pkt[1].u8[5]);
        // clang-format on
        embeddedCliPrint(cli, msg);
    } else {
        alignas(8) char tmp[128 + 8];
        stream_t        p_min[2], p_max[2];
        get_sim_cfg(2, p_min, 2);
        get_sim_cfg(2, p_max, 3);

        int n = snprintf(msg, 256, "Simulation Arg Ranges:");

        // clang-format off
        struct { const char *name; int min; int max; const char *unit; } rows[] = {
            {"Ramp Time", p_min[0].u8[1], p_max[0].u8[1], "sec"},
            {"Thermal Mass", p_min[0].u16[1], p_max[0].u16[1], "J/°C"},
            {"Conductance", p_min[0].u16[2], p_max[0].u16[2], "mW/°C"},
            {"Heater Power", p_min[0].u16[3], p_max[0].u16[3], "mW"},
            {"Ambient Temp", (int16_t)p_min[1].u16[3], (int16_t)p_max[1].u16[3], "°C"},
            {"Sensor latency", p_min[1].u8[1], p_max[1].u8[1], "ms"},
            {"Target Temp", p_min[1].u8[2], p_max[1].u8[2], "°C"},
            {"Critical Temp",p_min[1].u8[3], p_max[1].u8[3], "°C"},
            {"Cooldown Temp", p_min[1].u8[4], p_max[1].u8[4], "°C"},
            {"Hysteresis", p_min[1].u8[5], p_max[1].u8[5], "°C"}
        };
        // clang-format on

        for (int i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            int m = snprintf(tmp, 128, "\r\n %s: %d - %d %s", rows[i].name, rows[i].min, rows[i].max, rows[i].unit);

            if (n + m < 256) {
                memcpy(msg + n, tmp, ALIGN_UP(m, 8));
                n += m;
            } else {
                msg[n] = '\0';
                embeddedCliPrint(cli, msg);
                memcpy(msg, tmp + 2, ALIGN_UP(m - 2, 8)); // skip leading \r\n
                n = m - 2;
            }
        }
        msg[n] = '\0';
        embeddedCliPrint(cli, msg);
    }
#endif
}

void show_config_short(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Short config"); // placeholder
}

void show_config_log(EmbeddedCli *cli, char *args, int count)
{
    int             n = 12; // initial size of msg
    alignas(8) char msg[256 + 8] = "Log config: ";
    alignas(8) char tmp[64 + 8];
    bool            enabled = false;

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if (!(ent_map_en & BIT(eid)))
                continue;
            int level = log_get_level(did, eid);
            if (level) {
                if (!enabled)
                    enabled = true;
                // clang-format off
                int m = snprintf(tmp, 64, "\r\n [%s%s%s] %s", domain_name(did, 1), eid ? ":" : "",
                                 entity_name(eid, 1), level_name(level, 1));
                // clang-format on
                if (n + m < 256) {
                    memcpy(msg + n, tmp, ALIGN_UP(m, 8));
                    n += m;
                } else {
                    msg[n] = '\0';
                    embeddedCliPrint(cli, msg);
                    memcpy(msg, tmp + 2, ALIGN_UP(m - 2, 8));
                    n = m - 2;
                }
            }
        }
    }

    if (enabled) {
        msg[n] = '\0';
        embeddedCliPrint(cli, msg);
    } else
        embeddedCliPrint(cli, "No logs enabled");
}

void show_domains(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Domains:";
    int  n = 8;

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        n += snprintf(msg + n, sizeof(msg) - n, " %s", domain_name((uint8_t)did, 1));
    }
    embeddedCliPrint(cli, msg);
}

void show_entities(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Entities:";
    int  n = 9;

    for (int eid = 1; eid < NUM_ENTITIES; eid++) {
        if (!(ent_map_en & BIT(eid)))
            continue;
        n += snprintf(msg + n, sizeof(msg) - n, " %s", entity_name((uint8_t)eid, 1));
    }
    embeddedCliPrint(cli, msg);
}

void show_log_levels(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Log levels:";
    int  n = 11;

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        n += snprintf(msg + n, sizeof(msg) - n, " %s", level_name((uint8_t)level_id[i], 1));
    }
    embeddedCliPrint(cli, msg);
}
