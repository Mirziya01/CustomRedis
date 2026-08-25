#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s);
void sb_append_len(StrBuf *sb, const char *s, size_t len);
void sb_append_fmt(StrBuf *sb, const char *fmt, ...);
/* Hands ownership of the internal buffer to the caller (who must
 * free() it); the StrBuf itself is left empty/usable afterwards. */
char *sb_release(StrBuf *sb);

#endif /* STRBUF_H */
