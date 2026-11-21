#pragma once

void show_stats_sys(EmbeddedCli *cli, char *args, int count);
void show_stats_log(EmbeddedCli *cli, char *args, int count);
void show_stats_prof(EmbeddedCli *cli, char *args, int count);
void show_stats_task(EmbeddedCli *cli, char *args, int count);
void show_version(EmbeddedCli *cli, char *args, int count);
void show_config(EmbeddedCli *cli, char *args, int count);
void show_config_sim(EmbeddedCli *cli, char *args, int count);
void show_config_short(EmbeddedCli *cli, char *args, int count);
void show_config_log(EmbeddedCli *cli, char *args, int count);
void show_domains(EmbeddedCli *cli, char *args, int count);
void show_entities(EmbeddedCli *cli, char *args, int count);
void show_log_levels(EmbeddedCli *cli, char *args, int count);
