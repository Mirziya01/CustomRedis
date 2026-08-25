#include "../include/redis_command_handler.h"
#include "../include/redis_database.h"
#include "../include/strbuf.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * RESP parsing
 *
 * *2\r\n$4\r\nPING\r\n$4\r\nTEST\r\n
 * *2 -> array has 2 elements
 * $4 -> next string has 4 characters
 * PING
 * TEST
 *
 * Lengths and counts are parsed with strtol plus explicit error
 * checking (never a bare atoi/scanf on client-controlled input), so
 * a malformed length field can't crash the connection thread.
 * ------------------------------------------------------------------ */

typedef struct {
    char **tokens;
    size_t count;
    size_t cap;
} TokenVec;

static void tokenvec_init(TokenVec *tv) {
    tv->cap = 8;
    tv->count = 0;
    tv->tokens = malloc(sizeof(char *) * tv->cap);
}

static void tokenvec_push(TokenVec *tv, const char *data, size_t len) {
    if (tv->count == tv->cap) {
        tv->cap *= 2;
        tv->tokens = realloc(tv->tokens, sizeof(char *) * tv->cap);
    }
    char *tok = malloc(len + 1);
    memcpy(tok, data, len);
    tok[len] = '\0';
    tv->tokens[tv->count++] = tok;
}

void tokenvec_free(TokenVec *tv) {
    for (size_t i = 0; i < tv->count; i++) free(tv->tokens[i]);
    free(tv->tokens);
}

/* Finds the next "\r\n" at or after `from`, within the first `len`
 * bytes of `buf`. Returns the offset, or (size_t)-1 if not found. */
static size_t find_crlf(const char *buf, size_t len, size_t from) {
    for (size_t i = from; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return i;
    }
    return (size_t)-1;
}

/* Parses a decimal integer from buf[pos..crlf). Returns 1 on success
 * (writing *out), 0 on malformed input. */
static int parse_int_field(const char *buf, size_t pos, size_t crlf, long *out) {
    if (crlf <= pos) return 0;
    char tmp[32];
    size_t len = crlf - pos;
    if (len >= sizeof(tmp)) return 0;
    memcpy(tmp, buf + pos, len);
    tmp[len] = '\0';
    char *endptr = NULL;
    long v = strtol(tmp, &endptr, 10);
    if (endptr == tmp || *endptr != '\0') return 0;
    *out = v;
    return 1;
}

static void parse_resp_command(const char *input, size_t input_len, TokenVec *out) {
    tokenvec_init(out);
    if (input_len == 0) return;

    if (input[0] != '*') {
        /* Fallback: split on whitespace, for non-RESP input
         * (e.g. a raw telnet session). */
        size_t i = 0;
        while (i < input_len) {
            while (i < input_len && isspace((unsigned char)input[i])) i++;
            size_t start = i;
            while (i < input_len && !isspace((unsigned char)input[i])) i++;
            if (i > start) tokenvec_push(out, input + start, i - start);
        }
        return;
    }

    size_t pos = 1; /* skip '*' */
    size_t crlf = find_crlf(input, input_len, pos);
    if (crlf == (size_t)-1) return;

    long num_elements;
    if (!parse_int_field(input, pos, crlf, &num_elements) || num_elements < 0) return;
    pos = crlf + 2;

    for (long i = 0; i < num_elements; i++) {
        if (pos >= input_len || input[pos] != '$') break;
        pos++; /* skip '$' */

        crlf = find_crlf(input, input_len, pos);
        if (crlf == (size_t)-1) break;

        long len;
        if (!parse_int_field(input, pos, crlf, &len) || len < 0) break;
        pos = crlf + 2;

        if (pos + (size_t)len > input_len) break;
        tokenvec_push(out, input + pos, (size_t)len);
        pos += (size_t)len + 2; /* skip token and trailing CRLF */
    }
}

/* ------------------------------------------------------------------
 * Command handlers
 *
 * Each takes the parsed tokens and returns a malloc'd RESP response.
 * `tokens[0]` is always the (already-uppercased) command name.
 * ------------------------------------------------------------------ */

static char *resp_error(const char *msg) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "-Error: ");
    sb_append(&sb, msg);
    sb_append(&sb, "\r\n");
    return sb_release(&sb);
}

static char *resp_simple(const char *msg) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "+");
    sb_append(&sb, msg);
    sb_append(&sb, "\r\n");
    return sb_release(&sb);
}

static char *resp_bulk(const char *value) {
    StrBuf sb;
    sb_init(&sb);
    sb_append_fmt(&sb, "$%zu\r\n", strlen(value));
    sb_append(&sb, value);
    sb_append(&sb, "\r\n");
    return sb_release(&sb);
}

static char *resp_nil(void) { return strdup("$-1\r\n"); }

static char *resp_integer(long long v) {
    StrBuf sb;
    sb_init(&sb);
    sb_append_fmt(&sb, ":%lld\r\n", v);
    return sb_release(&sb);
}

static char *resp_string_array(char **items, size_t count) {
    StrBuf sb;
    sb_init(&sb);
    sb_append_fmt(&sb, "*%zu\r\n", count);
    for (size_t i = 0; i < count; i++) {
        sb_append_fmt(&sb, "$%zu\r\n", strlen(items[i]));
        sb_append(&sb, items[i]);
        sb_append(&sb, "\r\n");
    }
    return sb_release(&sb);
}

static void free_str_array(char **items, size_t count) {
    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);
}

/* -- Common -- */
static char *handle_ping(TokenVec *t, RedisDatabase *db) { (void)t; (void)db; return strdup("+PONG\r\n"); }

static char *handle_echo(TokenVec *t, RedisDatabase *db) {
    (void)db;
    if (t->count < 2) return resp_error("ECHO requires a message");
    return resp_bulk(t->tokens[1]);
}

static char *handle_flushall(TokenVec *t, RedisDatabase *db) {
    (void)t;
    rdb_flush_all(db);
    return resp_simple("OK");
}

/* -- Key/Value -- */
static char *handle_set(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("SET requires key and value");
    rdb_set(db, t->tokens[1], t->tokens[2]);
    return resp_simple("OK");
}

static char *handle_get(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("GET requires key");
    char *value;
    if (rdb_get(db, t->tokens[1], &value)) {
        char *r = resp_bulk(value);
        free(value);
        return r;
    }
    return resp_nil();
}

static char *handle_keys(TokenVec *t, RedisDatabase *db) {
    (void)t;
    size_t count;
    char **keys = rdb_keys(db, &count);
    char *r = resp_string_array(keys, count);
    free_str_array(keys, count);
    return r;
}

static char *handle_type(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("TYPE requires key");
    return resp_simple(rdb_type(db, t->tokens[1]));
}

static char *handle_del(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("DEL requires key");
    bool res = rdb_del(db, t->tokens[1]);
    return resp_integer(res ? 1 : 0);
}

static char *handle_expire(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("EXPIRE requires key and time in seconds");
    char *endptr = NULL;
    long seconds = strtol(t->tokens[2], &endptr, 10);
    if (endptr == t->tokens[2] || *endptr != '\0') return resp_error("Invalid expiration time");
    if (rdb_expire(db, t->tokens[1], (int)seconds)) return resp_simple("OK");
    return resp_error("Key not found");
}

static char *handle_rename(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("RENAME requires old key and new key");
    if (rdb_rename(db, t->tokens[1], t->tokens[2])) return resp_simple("OK");
    return resp_error("Key not found or rename failed");
}

/* -- List -- */
static char *handle_lget(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("LGET requires a key");
    size_t count;
    char **elems = rdb_lget(db, t->tokens[1], &count);
    char *r = resp_string_array(elems, count);
    free_str_array(elems, count);
    return r;
}

static char *handle_llen(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("LLEN requires key");
    return resp_integer(rdb_llen(db, t->tokens[1]));
}

static char *handle_lpush(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("LPUSH requires key and value");
    for (size_t i = 2; i < t->count; i++) rdb_lpush(db, t->tokens[1], t->tokens[i]);
    return resp_integer(rdb_llen(db, t->tokens[1]));
}

static char *handle_rpush(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("RPUSH requires key and value");
    for (size_t i = 2; i < t->count; i++) rdb_rpush(db, t->tokens[1], t->tokens[i]);
    return resp_integer(rdb_llen(db, t->tokens[1]));
}

static char *handle_lpop(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("LPOP requires key");
    char *val;
    if (rdb_lpop(db, t->tokens[1], &val)) {
        char *r = resp_bulk(val);
        free(val);
        return r;
    }
    return resp_nil();
}

static char *handle_rpop(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("RPOP requires key");
    char *val;
    if (rdb_rpop(db, t->tokens[1], &val)) {
        char *r = resp_bulk(val);
        free(val);
        return r;
    }
    return resp_nil();
}

static char *handle_lrem(TokenVec *t, RedisDatabase *db) {
    if (t->count < 4) return resp_error("LREM requires key, count and value");
    char *endptr = NULL;
    long count = strtol(t->tokens[2], &endptr, 10);
    if (endptr == t->tokens[2] || *endptr != '\0') return resp_error("Invalid count");
    int removed = rdb_lrem(db, t->tokens[1], (int)count, t->tokens[3]);
    return resp_integer(removed);
}

static char *handle_lindex(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("LINDEX requires key and index");
    char *endptr = NULL;
    long index = strtol(t->tokens[2], &endptr, 10);
    if (endptr == t->tokens[2] || *endptr != '\0') return resp_error("Invalid index");
    char *value;
    if (rdb_lindex(db, t->tokens[1], (int)index, &value)) {
        char *r = resp_bulk(value);
        free(value);
        return r;
    }
    return resp_nil();
}

static char *handle_lset(TokenVec *t, RedisDatabase *db) {
    if (t->count < 4) return resp_error("LSET requires key, index and value");
    char *endptr = NULL;
    long index = strtol(t->tokens[2], &endptr, 10);
    if (endptr == t->tokens[2] || *endptr != '\0') return resp_error("Invalid index");
    if (rdb_lset(db, t->tokens[1], (int)index, t->tokens[3])) return resp_simple("OK");
    return resp_error("Index out of range");
}

/* -- Hash -- */
static char *handle_hset(TokenVec *t, RedisDatabase *db) {
    if (t->count < 4) return resp_error("HSET requires key, field and value");
    rdb_hset(db, t->tokens[1], t->tokens[2], t->tokens[3]);
    return resp_integer(1);
}

static char *handle_hget(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("HGET requires key and field");
    char *value;
    if (rdb_hget(db, t->tokens[1], t->tokens[2], &value)) {
        char *r = resp_bulk(value);
        free(value);
        return r;
    }
    return resp_nil();
}

static char *handle_hexists(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("HEXISTS requires key and field");
    return resp_integer(rdb_hexists(db, t->tokens[1], t->tokens[2]) ? 1 : 0);
}

static char *handle_hdel(TokenVec *t, RedisDatabase *db) {
    if (t->count < 3) return resp_error("HDEL requires key and field");
    return resp_integer(rdb_hdel(db, t->tokens[1], t->tokens[2]) ? 1 : 0);
}

static char *handle_hgetall(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("HGETALL requires key");
    char **fields, **values;
    size_t count;
    rdb_hgetall(db, t->tokens[1], &fields, &values, &count);

    StrBuf sb;
    sb_init(&sb);
    sb_append_fmt(&sb, "*%zu\r\n", count * 2);
    for (size_t i = 0; i < count; i++) {
        sb_append_fmt(&sb, "$%zu\r\n", strlen(fields[i]));
        sb_append(&sb, fields[i]);
        sb_append(&sb, "\r\n");
        sb_append_fmt(&sb, "$%zu\r\n", strlen(values[i]));
        sb_append(&sb, values[i]);
        sb_append(&sb, "\r\n");
    }
    free_str_array(fields, count);
    free_str_array(values, count);
    return sb_release(&sb);
}

static char *handle_hkeys(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("HKEYS requires key");
    size_t count;
    char **keys = rdb_hkeys(db, t->tokens[1], &count);
    char *r = resp_string_array(keys, count);
    free_str_array(keys, count);
    return r;
}

static char *handle_hvals(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("HVALS requires key");
    size_t count;
    char **values = rdb_hvals(db, t->tokens[1], &count);
    char *r = resp_string_array(values, count);
    free_str_array(values, count);
    return r;
}

static char *handle_hlen(TokenVec *t, RedisDatabase *db) {
    if (t->count < 2) return resp_error("HLEN requires key");
    return resp_integer(rdb_hlen(db, t->tokens[1]));
}

static char *handle_hmset(TokenVec *t, RedisDatabase *db) {
    if (t->count < 4 || (t->count % 2) == 1) return resp_error("HMSET requires key followed by field value pairs");
    size_t pair_count = (t->count - 2) / 2;
    const char **fields = malloc(sizeof(char *) * pair_count);
    const char **values = malloc(sizeof(char *) * pair_count);
    for (size_t i = 0, tok = 2; i < pair_count; i++, tok += 2) {
        fields[i] = t->tokens[tok];
        values[i] = t->tokens[tok + 1];
    }
    rdb_hmset(db, t->tokens[1], fields, values, pair_count);
    free(fields);
    free(values);
    return resp_simple("OK");
}

/* ------------------------------------------------------------------ */

void redis_command_handler_init(RedisCommandHandler *handler) {
    handler->_unused = 0;
}

static void uppercase_inplace(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

char *redis_command_handler_process(RedisCommandHandler *handler, const char *input, size_t input_len) {
    (void)handler;
    TokenVec tokens;
    parse_resp_command(input, input_len, &tokens);

    if (tokens.count == 0) {
        tokenvec_free(&tokens);
        return resp_error("Empty command");
    }

    uppercase_inplace(tokens.tokens[0]);
    const char *cmd = tokens.tokens[0];
    RedisDatabase *db = redis_database_get_instance();

    char *result;
    if (strcmp(cmd, "PING") == 0) result = handle_ping(&tokens, db);
    else if (strcmp(cmd, "ECHO") == 0) result = handle_echo(&tokens, db);
    else if (strcmp(cmd, "FLUSHALL") == 0) result = handle_flushall(&tokens, db);
    else if (strcmp(cmd, "SET") == 0) result = handle_set(&tokens, db);
    else if (strcmp(cmd, "GET") == 0) result = handle_get(&tokens, db);
    else if (strcmp(cmd, "KEYS") == 0) result = handle_keys(&tokens, db);
    else if (strcmp(cmd, "TYPE") == 0) result = handle_type(&tokens, db);
    else if (strcmp(cmd, "DEL") == 0 || strcmp(cmd, "UNLINK") == 0) result = handle_del(&tokens, db);
    else if (strcmp(cmd, "EXPIRE") == 0) result = handle_expire(&tokens, db);
    else if (strcmp(cmd, "RENAME") == 0) result = handle_rename(&tokens, db);
    else if (strcmp(cmd, "LGET") == 0) result = handle_lget(&tokens, db);
    else if (strcmp(cmd, "LLEN") == 0) result = handle_llen(&tokens, db);
    else if (strcmp(cmd, "LPUSH") == 0) result = handle_lpush(&tokens, db);
    else if (strcmp(cmd, "RPUSH") == 0) result = handle_rpush(&tokens, db);
    else if (strcmp(cmd, "LPOP") == 0) result = handle_lpop(&tokens, db);
    else if (strcmp(cmd, "RPOP") == 0) result = handle_rpop(&tokens, db);
    else if (strcmp(cmd, "LREM") == 0) result = handle_lrem(&tokens, db);
    else if (strcmp(cmd, "LINDEX") == 0) result = handle_lindex(&tokens, db);
    else if (strcmp(cmd, "LSET") == 0) result = handle_lset(&tokens, db);
    else if (strcmp(cmd, "HSET") == 0) result = handle_hset(&tokens, db);
    else if (strcmp(cmd, "HGET") == 0) result = handle_hget(&tokens, db);
    else if (strcmp(cmd, "HEXISTS") == 0) result = handle_hexists(&tokens, db);
    else if (strcmp(cmd, "HDEL") == 0) result = handle_hdel(&tokens, db);
    else if (strcmp(cmd, "HGETALL") == 0) result = handle_hgetall(&tokens, db);
    else if (strcmp(cmd, "HKEYS") == 0) result = handle_hkeys(&tokens, db);
    else if (strcmp(cmd, "HVALS") == 0) result = handle_hvals(&tokens, db);
    else if (strcmp(cmd, "HLEN") == 0) result = handle_hlen(&tokens, db);
    else if (strcmp(cmd, "HMSET") == 0) result = handle_hmset(&tokens, db);
    else result = resp_error("Unknown command");

    tokenvec_free(&tokens);
    return result;
}
