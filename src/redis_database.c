#include "../include/redis_database.h"
#include "../include/strlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static RedisDatabase *g_instance = NULL;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

/* ---- helpers used as HashTable free callbacks ---- */
static void free_plain(void *p) { free(p); }

static void free_strlist_cb(void *p) { strlist_free((StrList *)p); }

static void free_hashtable_cb(void *p) { ht_destroy((HashTable *)p, free_plain); }

static void init_instance(void) {
    g_instance = malloc(sizeof(RedisDatabase));
    pthread_mutex_init(&g_instance->mutex, NULL);
    g_instance->kv_store = ht_create(64);
    g_instance->list_store = ht_create(64);
    g_instance->hash_store = ht_create(64);
    g_instance->expiry_map = ht_create(64);
}

RedisDatabase *redis_database_get_instance(void) {
    pthread_once(&g_once, init_instance);
    return g_instance;
}

void redis_database_shutdown(void) {
    if (!g_instance) return;
    ht_destroy(g_instance->kv_store, free_plain);
    ht_destroy(g_instance->list_store, free_strlist_cb);
    ht_destroy(g_instance->hash_store, free_hashtable_cb);
    ht_destroy(g_instance->expiry_map, free_plain);
    pthread_mutex_destroy(&g_instance->mutex);
    free(g_instance);
    g_instance = NULL;
}

typedef struct {
    time_t now;
    char **expired;
    size_t count;
    size_t cap;
} ExpiryScanCtx;

static void scan_expired_cb(const char *key, void *value, void *user) {
    ExpiryScanCtx *ctx = user;
    time_t deadline = *(time_t *)value;
    if (ctx->now > deadline) {
        if (ctx->count == ctx->cap) {
            ctx->cap = ctx->cap ? ctx->cap * 2 : 8;
            ctx->expired = realloc(ctx->expired, sizeof(char *) * ctx->cap);
        }
        ctx->expired[ctx->count++] = strdup(key);
    }
}

/* Removes any keys whose expiry has passed. Caller must hold db->mutex. */
static void purge_expired_locked(RedisDatabase *db) {
    /* Collect expired keys first -- we can't safely delete from
     * expiry_map while ht_foreach is iterating it. */
    ExpiryScanCtx ctx = { time(NULL), NULL, 0, 0 };
    ht_foreach(db->expiry_map, scan_expired_cb, &ctx);
    char **expired = ctx.expired;

    for (size_t i = 0; i < ctx.count; i++) {
        ht_del(db->kv_store, expired[i], free_plain);
        ht_del(db->list_store, expired[i], free_strlist_cb);
        ht_del(db->hash_store, expired[i], free_hashtable_cb);
        ht_del(db->expiry_map, expired[i], free_plain);
        free(expired[i]);
    }
    free(expired);
}

/* ---- Common commands ---- */
bool rdb_flush_all(RedisDatabase *db) {
    pthread_mutex_lock(&db->mutex);
    ht_clear(db->kv_store, free_plain);
    ht_clear(db->list_store, free_strlist_cb);
    ht_clear(db->hash_store, free_hashtable_cb);
    ht_clear(db->expiry_map, free_plain);
    pthread_mutex_unlock(&db->mutex);
    return true;
}

/* ---- Key/Value operations ---- */
void rdb_set(RedisDatabase *db, const char *key, const char *value) {
    pthread_mutex_lock(&db->mutex);
    ht_set(db->kv_store, key, strdup(value), free_plain);
    pthread_mutex_unlock(&db->mutex);
}

bool rdb_get(RedisDatabase *db, const char *key, char **out_value) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    char *v = ht_get(db->kv_store, key);
    bool found = v != NULL;
    if (found) *out_value = strdup(v);
    pthread_mutex_unlock(&db->mutex);
    return found;
}

typedef struct {
    char **arr;
    size_t idx;
} CollectCtx;

static void collect_key_cb(const char *key, void *value, void *user) {
    (void)value;
    CollectCtx *ctx = user;
    ctx->arr[ctx->idx++] = strdup(key);
}

char **rdb_keys(RedisDatabase *db, size_t *out_count) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    size_t total = ht_size(db->kv_store) + ht_size(db->list_store) + ht_size(db->hash_store);
    char **result = malloc(sizeof(char *) * (total ? total : 1));
    CollectCtx ctx = { result, 0 };
    ht_foreach(db->kv_store, collect_key_cb, &ctx);
    ht_foreach(db->list_store, collect_key_cb, &ctx);
    ht_foreach(db->hash_store, collect_key_cb, &ctx);
    pthread_mutex_unlock(&db->mutex);
    *out_count = total;
    return result;
}

const char *rdb_type(RedisDatabase *db, const char *key) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    const char *result = "none";
    if (ht_exists(db->kv_store, key)) result = "string";
    else if (ht_exists(db->list_store, key)) result = "list";
    else if (ht_exists(db->hash_store, key)) result = "hash";
    pthread_mutex_unlock(&db->mutex);
    return result;
}

bool rdb_del(RedisDatabase *db, const char *key) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    bool erased = false;
    /* Bug fix vs. the original C++: that version computed `erased`
     * via |= across all three stores but then always `return false`.
     * Here the accumulated result is actually returned. */
    erased |= ht_del(db->kv_store, key, free_plain) != 0;
    erased |= ht_del(db->list_store, key, free_strlist_cb) != 0;
    erased |= ht_del(db->hash_store, key, free_hashtable_cb) != 0;
    ht_del(db->expiry_map, key, free_plain);
    pthread_mutex_unlock(&db->mutex);
    return erased;
}

bool rdb_expire(RedisDatabase *db, const char *key, int seconds) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    bool exists = ht_exists(db->kv_store, key) ||
                  ht_exists(db->list_store, key) ||
                  ht_exists(db->hash_store, key);
    if (exists) {
        time_t *deadline = malloc(sizeof(time_t));
        *deadline = time(NULL) + seconds;
        ht_set(db->expiry_map, key, deadline, free_plain);
    }
    pthread_mutex_unlock(&db->mutex);
    return exists;
}

bool rdb_rename(RedisDatabase *db, const char *old_key, const char *new_key) {
    pthread_mutex_lock(&db->mutex);
    purge_expired_locked(db);
    bool found = false;
    found |= ht_rename(db->kv_store, old_key, new_key, free_plain) != 0;
    found |= ht_rename(db->list_store, old_key, new_key, free_strlist_cb) != 0;
    found |= ht_rename(db->hash_store, old_key, new_key, free_hashtable_cb) != 0;
    ht_rename(db->expiry_map, old_key, new_key, free_plain);
    pthread_mutex_unlock(&db->mutex);
    return found;
}

/* ---- List operations ---- */
char **rdb_lget(RedisDatabase *db, const char *key, size_t *out_count) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    size_t count = list ? list->count : 0;
    char **result = malloc(sizeof(char *) * (count ? count : 1));
    for (size_t i = 0; i < count; i++) result[i] = strdup(list->items[i]);
    pthread_mutex_unlock(&db->mutex);
    *out_count = count;
    return result;
}

ssize_t rdb_llen(RedisDatabase *db, const char *key) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    ssize_t len = list ? (ssize_t)list->count : 0;
    pthread_mutex_unlock(&db->mutex);
    return len;
}

static StrList *get_or_create_list(RedisDatabase *db, const char *key) {
    StrList *list = ht_get(db->list_store, key);
    if (!list) {
        list = strlist_create();
        ht_set(db->list_store, key, list, free_strlist_cb);
    }
    return list;
}

void rdb_lpush(RedisDatabase *db, const char *key, const char *value) {
    pthread_mutex_lock(&db->mutex);
    strlist_push_front(get_or_create_list(db, key), value);
    pthread_mutex_unlock(&db->mutex);
}

void rdb_rpush(RedisDatabase *db, const char *key, const char *value) {
    pthread_mutex_lock(&db->mutex);
    strlist_push_back(get_or_create_list(db, key), value);
    pthread_mutex_unlock(&db->mutex);
}

bool rdb_lpop(RedisDatabase *db, const char *key, char **out_value) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    bool ok = list && strlist_pop_front(list, out_value);
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

bool rdb_rpop(RedisDatabase *db, const char *key, char **out_value) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    bool ok = list && strlist_pop_back(list, out_value);
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

int rdb_lrem(RedisDatabase *db, const char *key, int count, const char *value) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    int removed = 0;
    if (list) {
        if (count == 0) {
            for (size_t i = 0; i < list->count; ) {
                if (strcmp(list->items[i], value) == 0) {
                    strlist_remove_at(list, i);
                    removed++;
                } else {
                    i++;
                }
            }
        } else if (count > 0) {
            for (size_t i = 0; i < list->count && removed < count; ) {
                if (strcmp(list->items[i], value) == 0) {
                    strlist_remove_at(list, i);
                    removed++;
                } else {
                    i++;
                }
            }
        } else {
            int target = -count;
            for (long i = (long)list->count - 1; i >= 0 && removed < target; i--) {
                if (strcmp(list->items[i], value) == 0) {
                    strlist_remove_at(list, (size_t)i);
                    removed++;
                }
            }
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return removed;
}

bool rdb_lindex(RedisDatabase *db, const char *key, int index, char **out_value) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    bool ok = false;
    if (list) {
        int idx = index;
        if (idx < 0) idx = (int)list->count + idx;
        if (idx >= 0 && idx < (int)list->count) {
            *out_value = strdup(list->items[idx]);
            ok = true;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

bool rdb_lset(RedisDatabase *db, const char *key, int index, const char *value) {
    pthread_mutex_lock(&db->mutex);
    StrList *list = ht_get(db->list_store, key);
    bool ok = false;
    if (list) {
        int idx = index;
        if (idx < 0) idx = (int)list->count + idx;
        if (idx >= 0 && idx < (int)list->count) {
            free(list->items[idx]);
            list->items[idx] = strdup(value);
            ok = true;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return ok;
}

/* ---- Hash operations ---- */
static HashTable *get_or_create_hash(RedisDatabase *db, const char *key) {
    HashTable *h = ht_get(db->hash_store, key);
    if (!h) {
        h = ht_create(16);
        ht_set(db->hash_store, key, h, free_hashtable_cb);
    }
    return h;
}

bool rdb_hset(RedisDatabase *db, const char *key, const char *field, const char *value) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = get_or_create_hash(db, key);
    ht_set(h, field, strdup(value), free_plain);
    pthread_mutex_unlock(&db->mutex);
    return true;
}

bool rdb_hget(RedisDatabase *db, const char *key, const char *field, char **out_value) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    char *v = h ? ht_get(h, field) : NULL;
    bool found = v != NULL;
    if (found) *out_value = strdup(v);
    pthread_mutex_unlock(&db->mutex);
    return found;
}

bool rdb_hexists(RedisDatabase *db, const char *key, const char *field) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    bool exists = h && ht_exists(h, field);
    pthread_mutex_unlock(&db->mutex);
    return exists;
}

bool rdb_hdel(RedisDatabase *db, const char *key, const char *field) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    bool removed = h && ht_del(h, field, free_plain);
    pthread_mutex_unlock(&db->mutex);
    return removed;
}

typedef struct {
    char **fields;
    char **values;
    size_t idx;
} HashCollectCtx;

static void collect_field_value_cb(const char *key, void *value, void *user) {
    HashCollectCtx *ctx = user;
    ctx->fields[ctx->idx] = strdup(key);
    ctx->values[ctx->idx] = strdup((char *)value);
    ctx->idx++;
}

bool rdb_hgetall(RedisDatabase *db, const char *key, char ***out_fields, char ***out_values, size_t *out_count) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    size_t count = h ? ht_size(h) : 0;
    char **fields = malloc(sizeof(char *) * (count ? count : 1));
    char **values = malloc(sizeof(char *) * (count ? count : 1));
    if (h) {
        HashCollectCtx ctx = { fields, values, 0 };
        ht_foreach(h, collect_field_value_cb, &ctx);
    }
    pthread_mutex_unlock(&db->mutex);
    *out_fields = fields;
    *out_values = values;
    *out_count = count;
    return true;
}

char **rdb_hkeys(RedisDatabase *db, const char *key, size_t *out_count) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    size_t count = h ? ht_size(h) : 0;
    char **result = malloc(sizeof(char *) * (count ? count : 1));
    if (h) {
        CollectCtx ctx = { result, 0 };
        ht_foreach(h, collect_key_cb, &ctx);
    }
    pthread_mutex_unlock(&db->mutex);
    *out_count = count;
    return result;
}

static void collect_value_cb(const char *key, void *value, void *user) {
    (void)key;
    CollectCtx *ctx = user;
    ctx->arr[ctx->idx++] = strdup((char *)value);
}

char **rdb_hvals(RedisDatabase *db, const char *key, size_t *out_count) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    size_t count = h ? ht_size(h) : 0;
    char **result = malloc(sizeof(char *) * (count ? count : 1));
    if (h) {
        CollectCtx ctx = { result, 0 };
        ht_foreach(h, collect_value_cb, &ctx);
    }
    pthread_mutex_unlock(&db->mutex);
    *out_count = count;
    return result;
}

ssize_t rdb_hlen(RedisDatabase *db, const char *key) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = ht_get(db->hash_store, key);
    ssize_t len = h ? (ssize_t)ht_size(h) : 0;
    pthread_mutex_unlock(&db->mutex);
    return len;
}

bool rdb_hmset(RedisDatabase *db, const char *key, const char **fields, const char **values, size_t count) {
    pthread_mutex_lock(&db->mutex);
    HashTable *h = get_or_create_hash(db, key);
    for (size_t i = 0; i < count; i++) {
        ht_set(h, fields[i], strdup(values[i]), free_plain);
    }
    pthread_mutex_unlock(&db->mutex);
    return true;
}

/* ---- Persistence ----
 *
 * The original C++ implementation wrote "K key value\n" and split on
 * whitespace when loading, which silently corrupts any key or value
 * that itself contains a space. This version uses a length-prefixed
 * token format ("<byte-length>:<raw-bytes>") so arbitrary values
 * (including embedded spaces) round-trip correctly. Values containing
 * a literal newline are still unsupported, since records are still
 * one-per-line; that's a reasonable simplification for this project's
 * scope.
 */
static void write_token(FILE *f, const char *s) {
    fprintf(f, "%zu:%s ", strlen(s), s);
}

/* Reads one length-prefixed token from f into a malloc'd buffer.
 * Returns NULL on EOF/format error. */
static char *read_token(FILE *f) {
    size_t len;
    if (fscanf(f, "%zu:", &len) != 1) return NULL;
    char *buf = malloc(len + 1);
    size_t got = fread(buf, 1, len, f);
    if (got != len) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    fgetc(f); /* consume the single trailing space separator */
    return buf;
}

typedef struct { FILE *f; } DumpCtx;

static void dump_kv_cb(const char *key, void *value, void *user) {
    FILE *f = ((DumpCtx *)user)->f;
    fprintf(f, "K ");
    write_token(f, key);
    write_token(f, (char *)value);
    fprintf(f, "\n");
}

static void dump_list_cb(const char *key, void *value, void *user) {
    FILE *f = ((DumpCtx *)user)->f;
    StrList *list = value;
    fprintf(f, "L ");
    write_token(f, key);
    fprintf(f, "%zu ", list->count);
    for (size_t i = 0; i < list->count; i++) write_token(f, list->items[i]);
    fprintf(f, "\n");
}

static void dump_hash_field_cb(const char *field, void *value, void *user) {
    FILE *f = user;
    write_token(f, field);
    write_token(f, (char *)value);
}

static void dump_hash_cb(const char *key, void *value, void *user) {
    FILE *f = ((DumpCtx *)user)->f;
    HashTable *h = value;
    fprintf(f, "H ");
    write_token(f, key);
    fprintf(f, "%zu ", ht_size(h));
    ht_foreach(h, dump_hash_field_cb, f);
    fprintf(f, "\n");
}

bool rdb_dump(RedisDatabase *db, const char *filename) {
    pthread_mutex_lock(&db->mutex);
    FILE *f = fopen(filename, "wb");
    if (!f) {
        pthread_mutex_unlock(&db->mutex);
        return false;
    }
    DumpCtx ctx = { f };
    ht_foreach(db->kv_store, dump_kv_cb, &ctx);
    ht_foreach(db->list_store, dump_list_cb, &ctx);
    ht_foreach(db->hash_store, dump_hash_cb, &ctx);
    fclose(f);
    pthread_mutex_unlock(&db->mutex);
    return true;
}

bool rdb_load(RedisDatabase *db, const char *filename) {
    pthread_mutex_lock(&db->mutex);
    FILE *f = fopen(filename, "rb");
    if (!f) {
        pthread_mutex_unlock(&db->mutex);
        return false;
    }

    ht_clear(db->kv_store, free_plain);
    ht_clear(db->list_store, free_strlist_cb);
    ht_clear(db->hash_store, free_hashtable_cb);

    int type;
    while ((type = fgetc(f)) != EOF) {
        if (type == '\n' || type == '\r' || type == ' ') continue;
        fgetc(f); /* consume the space after the record-type letter */

        char *key = read_token(f);
        if (!key) break;

        if (type == 'K') {
            char *value = read_token(f);
            if (value) ht_set(db->kv_store, key, value, free_plain);
        } else if (type == 'L') {
            size_t count;
            if (fscanf(f, "%zu ", &count) != 1) { free(key); break; }
            StrList *list = strlist_create();
            for (size_t i = 0; i < count; i++) {
                char *item = read_token(f);
                if (!item) break;
                strlist_push_back(list, item);
                free(item);
            }
            ht_set(db->list_store, key, list, free_strlist_cb);
        } else if (type == 'H') {
            size_t count;
            if (fscanf(f, "%zu ", &count) != 1) { free(key); break; }
            HashTable *h = ht_create(16);
            for (size_t i = 0; i < count; i++) {
                char *field = read_token(f);
                char *value = read_token(f);
                if (!field || !value) { free(field); free(value); break; }
                ht_set(h, field, value, free_plain);
                free(field);
            }
            ht_set(db->hash_store, key, h, free_hashtable_cb);
        }
        free(key);
    }

    fclose(f);
    pthread_mutex_unlock(&db->mutex);
    return true;
}
