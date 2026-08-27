#include "../include/command_handler.h"
#include "../include/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char **command_handler_split_args(const char *input, int *out_count) {
    size_t cap = 8;
    char **tokens = malloc(cap * sizeof(char *));
    int count = 0;

    const char *p = input;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        StrBuf sb;
        strbuf_init(&sb);

        if (*p == '"') {
            p++; /* skip opening quote */
            while (*p && *p != '"') {
                strbuf_append_char(&sb, *p);
                p++;
            }
            if (*p == '"') p++; /* skip closing quote */
        } else {
            while (*p && !isspace((unsigned char)*p)) {
                strbuf_append_char(&sb, *p);
                p++;
            }
        }

        if ((size_t)count >= cap) {
            cap *= 2;
            tokens = realloc(tokens, cap * sizeof(char *));
        }
        tokens[count++] = strbuf_take(&sb);
        strbuf_free(&sb);
    }

    if (count == 0) {
        free(tokens);
        tokens = NULL;
    }
    *out_count = count;
    return tokens;
}

void command_handler_free_args(char **args, int count) {
    if (!args) return;
    for (int i = 0; i < count; ++i) free(args[i]);
    free(args);
}

/*
* -> start of an array
$ -> bulk of string
*/
char *command_handler_build_resp(char **args, int count, size_t *out_len) {
    StrBuf sb;
    strbuf_init(&sb);

    char header[32];
    snprintf(header, sizeof(header), "*%d\r\n", count);
    strbuf_append_str(&sb, header);

    for (int i = 0; i < count; ++i) {
        size_t arg_len = strlen(args[i]);
        char len_header[32];
        snprintf(len_header, sizeof(len_header), "$%zu\r\n", arg_len);
        strbuf_append_str(&sb, len_header);
        strbuf_append(&sb, args[i], arg_len);
        strbuf_append_str(&sb, "\r\n");
    }

    *out_len = sb.len;
    return strbuf_take(&sb);
}
