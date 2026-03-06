/**
 * @file cli_no_test.c
 * @brief API stubs for no test build.
 */

#include "embedded_cli.h"

void show_test_menu(EmbeddedCli *cli)
{
    (void)cli;
}

void show_test_select_menu(EmbeddedCli *cli)
{
    (void)cli;
}

void set_test_commands(EmbeddedCli *cli)
{
    (void)cli;
}

void on_test_menu(EmbeddedCli *cli, char *args, void *context)
{
    (void)cli;
    (void)args;
    (void)context;
}

void on_enter_tests(EmbeddedCli *cli, char *args, void *context)
{
    (void)cli;
    (void)args;
    (void)context;
}
