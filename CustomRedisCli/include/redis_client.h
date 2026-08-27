#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <stddef.h>

/* RedisClient: opens/holds a TCP connection to a Redis server. */
typedef struct {
    char *host;   /* owned copy of the hostname */
    int   port;
    int   sockfd; /* -1 when not connected */
} RedisClient;

/* Initializes the struct (does not connect). Copies host. */
void redis_client_init(RedisClient *rc, const char *host, int port);

/* Resolves and connects (IPv4/IPv6 via getaddrinfo). Returns 1 on success, 0 on failure. */
int redis_client_connect(RedisClient *rc);

/* Closes the socket if open. Safe to call multiple times. */
void redis_client_disconnect(RedisClient *rc);

/* Frees owned memory (host). Does NOT close the socket - call disconnect first. */
void redis_client_destroy(RedisClient *rc);

int redis_client_get_socket_fd(const RedisClient *rc);

/* Sends the full command buffer, retrying on short writes. Returns 1 on success, 0 on failure. */
int redis_client_send_command(RedisClient *rc, const char *command, size_t len);

#endif /* REDIS_CLIENT_H */
