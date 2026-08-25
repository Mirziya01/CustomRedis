#include "../include/redis_server.h"
#include "../include/redis_command_handler.h"
#include "../include/redis_database.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Global pointer so the signal handler can reach the running server.
 * A signal handler can't take extra arguments, so there is no way
 * around some form of global state here. */
static RedisServer *g_server = NULL;

static void dump_or_report(void) {
    if (rdb_dump(redis_database_get_instance(), "dump.my_rdb"))
        printf("Database Dumped to dump.my_rdb\n");
    else
        fprintf(stderr, "Error dumping database\n");
}

static void signal_handler(int signum) {
    if (g_server) {
        printf("\nCaught signal %d, shutting down...\n", signum);
        redis_server_shutdown(g_server);
    }
    /* _exit() skips the normal atexit/stdio-flush cleanup that a
     * regular return from main() would get, so flush explicitly
     * here -- otherwise the log lines above can be lost, buffered
     * but never written out before the process ends. */
    fflush(stdout);
    fflush(stderr);
    _exit(signum);
}

static void setup_signal_handler(void) {
    signal(SIGINT, signal_handler);
}

void redis_server_init(RedisServer *server, int port) {
    server->port = port;
    server->server_socket = -1;
    server->running = 1;
    g_server = server;
    setup_signal_handler();
}

void redis_server_shutdown(RedisServer *server) {
    server->running = 0;
    if (server->server_socket != -1) {
        dump_or_report();
        close(server->server_socket);
        server->server_socket = -1;
    }
    printf("Server Shutdown Complete!\n");
}

typedef struct {
    int client_socket;
} ClientArgs;

static void *client_thread_main(void *arg) {
    ClientArgs *args = arg;
    int client_socket = args->client_socket;
    free(args);

    RedisCommandHandler handler;
    redis_command_handler_init(&handler);

    char buffer[1024];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        char *response = redis_command_handler_process(&handler, buffer, (size_t)bytes);
        send(client_socket, response, strlen(response), 0);
        free(response);
    }

    close(client_socket);
    return NULL;
}

void redis_server_run(RedisServer *server) {
    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket < 0) {
        fprintf(stderr, "Error Creating Server Socket\n");
        return;
    }

    int opt = 1;
    setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)server->port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error Binding Server Socket\n");
        return;
    }

    if (listen(server->server_socket, 10) < 0) {
        fprintf(stderr, "Error Listening On Server Socket\n");
        return;
    }

    printf("Redis Server Listening On Port %d\n", server->port);

    while (server->running) {
        int client_socket = accept(server->server_socket, NULL, NULL);
        if (client_socket < 0) {
            if (server->running)
                fprintf(stderr, "Error Accepting Client Connection\n");
            break;
        }

        ClientArgs *args = malloc(sizeof(ClientArgs));
        args->client_socket = client_socket;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread_main, args) != 0) {
            fprintf(stderr, "Error Spawning Client Thread\n");
            close(client_socket);
            free(args);
            continue;
        }
        /* Detach rather than collecting into a joinable-thread vector:
         * connections are independent and short-lived, so there's no
         * need to keep every past thread handle alive until process
         * exit just to join it. Each thread cleans up its own socket
         * when the client disconnects. */
        pthread_detach(tid);
    }

    if (server->server_socket != -1) {
        dump_or_report();
    }
}
