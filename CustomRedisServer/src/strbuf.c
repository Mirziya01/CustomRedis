#include "../include/strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRBUF_INITIAL_CAP 128

void sb_init(StrBuf *sb) {
    sb->cap = STRBUF_INITIAL_CAP;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

static void sb_ensure_cap(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t new_cap = sb->cap * 2;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    sb->data = realloc(sb->data, new_cap);
    sb->cap = new_cap;
}

void sb_append_len(StrBuf *sb, const char *s, size_t len) {
    sb_ensure_cap(sb, len);
    memcpy(sb->data + sb->len, s, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    sb_append_len(sb, s, strlen(s));
}

void sb_append_fmt(StrBuf *sb, const char *fmt, ...) {
    char stack_buf[64];
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
    va_end(ap);

    if (needed < 0) return;
    if ((size_t)needed < sizeof(stack_buf)) {
        sb_append_len(sb, stack_buf, (size_t)needed);
        return;
    }

    char *heap_buf = malloc((size_t)needed + 1);
    va_start(ap, fmt);
    vsnprintf(heap_buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    sb_append_len(sb, heap_buf, (size_t)needed);
    free(heap_buf);
}

char *sb_release(StrBuf *sb) {
    char *out = sb->data;
    sb_init(sb);
    return out;
}
