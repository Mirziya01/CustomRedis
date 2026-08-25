#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <stddef.h>

/* Stateless for now (RedisDatabase is its own singleton), kept as a
 * struct in case per-connection state is needed later. */
typedef struct RedisCommandHandler {
    int _unused;
} RedisCommandHandler;

void redis_command_handler_init(RedisCommandHandler *handler);

/* Parses `input` (which may contain a RESP array or a plain
 * whitespace-separated fallback command) and returns a newly
 * malloc'd, NUL-terminated RESP response string that the caller must
 * free(). `input_len` is the number of valid bytes in `input`. */
char *redis_command_handler_process(RedisCommandHandler *handler, const char *input, size_t input_len);

#endif /* REDIS_COMMAND_HANDLER_H */
