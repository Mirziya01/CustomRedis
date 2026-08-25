#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <signal.h>

typedef struct RedisServer {
    int port;
    int server_socket;
    volatile sig_atomic_t running;
} RedisServer;

void redis_server_init(RedisServer *server, int port);
void redis_server_run(RedisServer *server);
void redis_server_shutdown(RedisServer *server);

#endif /* REDIS_SERVER_H */
