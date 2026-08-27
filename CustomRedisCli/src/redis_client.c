/*
Establishing a TCP Connection to Redis (RedisClient)
    Uses Berkeley sockets to open a TCP connection to the Redis server.
    Supports IPv4 and IPv6 resolution using getaddrinfo.
*/

#include "../include/redis_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>

void redis_client_init(RedisClient *rc, const char *host, int port) {
    rc->host = strdup(host);
    rc->port = port;
    rc->sockfd = -1;
}

int redis_client_connect(RedisClient *rc) {
    struct addrinfo hints, *res = NULL, *p;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;   /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP */

    snprintf(port_str, sizeof(port_str), "%d", rc->port);

    int err = getaddrinfo(rc->host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return 0;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        rc->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (rc->sockfd == -1) continue;
        if (connect(rc->sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(rc->sockfd);
        rc->sockfd = -1;
    }
    freeaddrinfo(res);

    if (rc->sockfd == -1) {
        fprintf(stderr, "Could not connect to %s:%d\n", rc->host, rc->port);
        return 0;
    }
    return 1;
}

void redis_client_disconnect(RedisClient *rc) {
    if (rc->sockfd != -1) {
        close(rc->sockfd);
        rc->sockfd = -1;
    }
}

void redis_client_destroy(RedisClient *rc) {
    free(rc->host);
    rc->host = NULL;
}

int redis_client_get_socket_fd(const RedisClient *rc) {
    return rc->sockfd;
}

int redis_client_send_command(RedisClient *rc, const char *command, size_t len) {
    if (rc->sockfd == -1) return 0;

    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(rc->sockfd, command + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (sent == 0) return 0;
        total_sent += (size_t)sent;
    }
    return 1;
}
