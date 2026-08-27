#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stddef.h>

/* Splits a line of input into argument tokens, honoring double-quoted
 * substrings (e.g. SET key "hello world"). Returns a malloc'd array of
 * malloc'd strings; *out_count is set to the number of tokens. The caller
 * must free it with command_handler_free_args(). Returns NULL (with
 * *out_count == 0) if the input contains no tokens. */
char **command_handler_split_args(const char *input, int *out_count);

void command_handler_free_args(char **args, int count);

/* Builds a RESP-encoded command from args. Returns a malloc'd buffer
 * (NOT NUL-terminated meaningfully for binary safety - use *out_len) that
 * the caller must free(). */
char *command_handler_build_resp(char **args, int count, size_t *out_len);

#endif /* COMMAND_HANDLER_H */
