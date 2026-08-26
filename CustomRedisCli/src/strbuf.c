#include "strbuf.h"
#include <stdlib.h>
#include <string.h>

void strbuf_init(StrBuf *sb) {
    sb->data = malloc(1);
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = 1;
}

static void strbuf_ensure(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t new_cap = sb->cap ? sb->cap : 1;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    sb->data = realloc(sb->data, new_cap);
    sb->cap = new_cap;
}

void strbuf_append_char(StrBuf *sb, char c) {
    strbuf_ensure(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void strbuf_append(StrBuf *sb, const char *s, size_t n) {
    strbuf_ensure(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void strbuf_append_str(StrBuf *sb, const char *s) {
    strbuf_append(sb, s, strlen(s));
}

char *strbuf_take(StrBuf *sb) {
    char *out = sb->data;
    sb->data = malloc(1);
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = 1;
    return out;
}

void strbuf_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}
