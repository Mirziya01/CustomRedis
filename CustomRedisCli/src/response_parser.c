#include "response_parser.h"
#include "strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

static char *dupstr(const char *s) {
    char *r = strdup(s);
    return r;
}

/* Reads a single character from the socket. Returns 1 on success, 0 on
 * EOF/error (retries transparently on EINTR). */
static int read_char(int sockfd, char *c) {
    for (;;) {
        ssize_t r = recv(sockfd, c, 1, 0);
        if (r == 1) return 1;
        if (r < 0 && errno == EINTR) continue;
        return 0; /* connection closed or real error */
    }
}

/* Reads a line up to (and consuming) the trailing CRLF. Returns a malloc'd,
 * NUL-terminated string (caller frees). On a dropped connection mid-line,
 * returns whatever was read so far instead of looping forever - this fixes
 * a bug in the original where a closed socket mid-line could spin. */
static char *read_line(int sockfd) {
    StrBuf sb;
    strbuf_init(&sb);
    char c;
    while (read_char(sockfd, &c)) {
        if (c == '\r') {
            /* expect '\n' next */
            read_char(sockfd, &c);
            break;
        }
        strbuf_append_char(&sb, c);
    }
    return strbuf_take(&sb);
}

static char *parse_response_internal(int sockfd);

static char *parse_simple_string(int sockfd) {
    return read_line(sockfd);
}

static char *parse_simple_error(int sockfd) {
    char *line = read_line(sockfd);
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "(Error) ");
    strbuf_append_str(&sb, line);
    free(line);
    return strbuf_take(&sb);
}

static char *parse_integer(int sockfd) {
    return read_line(sockfd);
}

/* Parses a decimal integer strictly, rejecting empty/garbage input 
 * Returns 1 on success and writes the value to *out. */
static int parse_len(const char *s, long *out) {
    if (s == NULL || *s == '\0') return 0;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    *out = v;
    return 1;
}

static char *parse_bulk_string(int sockfd) {
    char *len_str = read_line(sockfd);
    long length;
    int ok = parse_len(len_str, &length);
    free(len_str);

    if (!ok) {
        return dupstr("(Error) Malformed bulk string length.");
    }
    if (length == -1) {
        return dupstr("(nil)");
    }
    if (length < -1) {
        return dupstr("(Error) Invalid bulk string length.");
    }

    char *bulk = malloc((size_t)length + 1);
    size_t total_read = 0;
    while (total_read < (size_t)length) {
        ssize_t r = recv(sockfd, bulk + total_read, (size_t)length - total_read, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            free(bulk);
            return dupstr("(Error) Incomplete bulk data.");
        }
        total_read += (size_t)r;
    }
    bulk[length] = '\0';

    /* Consume trailing CRLF */
    char dummy;
    read_char(sockfd, &dummy);
    read_char(sockfd, &dummy);

    return bulk;
}

static char *parse_array(int sockfd) {
    char *count_str = read_line(sockfd);
    long count;
    int ok = parse_len(count_str, &count);
    free(count_str);

    if (!ok) {
        return dupstr("(Error) Malformed array length.");
    }
    if (count == -1) {
        return dupstr("(nil)");
    }
    if (count < -1) {
        return dupstr("(Error) Invalid array length.");
    }

    StrBuf sb;
    strbuf_init(&sb);
    for (long i = 0; i < count; ++i) {
        char *item = parse_response_internal(sockfd);
        strbuf_append_str(&sb, item);
        free(item);
        if (i != count - 1) {
            strbuf_append_char(&sb, '\n');
        }
    }
    return strbuf_take(&sb);
}

static char *parse_response_internal(int sockfd) {
    char prefix;
    if (!read_char(sockfd, &prefix)) {
        return dupstr("(Error) No response or connection closed.");
    }
    switch (prefix) {
        case '+': return parse_simple_string(sockfd);
        case '-': return parse_simple_error(sockfd);
        case ':': return parse_integer(sockfd);
        case '$': return parse_bulk_string(sockfd);
        case '*': return parse_array(sockfd);
        default:  return dupstr("(Error) Unknown reply type.");
    }
}

char *response_parser_parse(int sockfd) {
    return parse_response_internal(sockfd);
}
