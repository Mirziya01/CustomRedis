#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/cli.h"

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 6379;
    int i = 1;

    /* One-shot command args, if any (points into argv - not owned). */
    char **command_args = NULL;
    int command_arg_count = 0;

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else {
            command_args = &argv[i];
            command_arg_count = argc - i;
            break;
        }
        ++i;
    }

    CLI cli;
    cli_init(&cli, host, port);
    cli_run(&cli, command_args, command_arg_count);
    cli_destroy(&cli);

    return 0;
}
