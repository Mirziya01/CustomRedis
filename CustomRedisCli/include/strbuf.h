#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

/* A minimal growable string buffer. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

void strbuf_init(StrBuf *sb);
void strbuf_append_char(StrBuf *sb, char c);
void strbuf_append(StrBuf *sb, const char *s, size_t n);
void strbuf_append_str(StrBuf *sb, const char *s);
/* Releases ownership of the internal buffer to the caller (caller must free()).
 * The StrBuf is reset to an empty, safely-destructible state. */
char *strbuf_take(StrBuf *sb);
void strbuf_free(StrBuf *sb);

#endif /* STRBUF_H */
