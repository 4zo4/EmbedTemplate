/**
 * @file cli_no_sim.c
 * @brief API stubs for build without simulation.
 */
#include <stdint.h>

#include "pack.h"

int set_sim_cfg(int len, stream_t *cfg)
{
    (void)len;
    (void)cfg;
    return 0;
}

int get_sim_cfg(int len, stream_t *cfg, bool cold)
{
    (void)len;
    (void)cfg;
    (void)cold;
    return 0;
}