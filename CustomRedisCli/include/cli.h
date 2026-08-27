#ifndef CLI_H
#define CLI_H

#include "redis_client.h"

typedef struct {
    char *host;
    int   port;
    RedisClient redisClient;
} CLI;

void cli_init(CLI *cli, const char *host, int port);
void cli_destroy(CLI *cli);

/* commandArgs/commandArgCount: an optional one-shot command (from argv). */
void cli_run(CLI *cli, char **commandArgs, int commandArgCount);
void cli_execute_command(CLI *cli, char **args, int count);
/* Handles pub-sub SUBSCRIBE mode. */
void cli_handle_subscription(CLI *cli, char **args, int count);

#endif /* CLI_H */
