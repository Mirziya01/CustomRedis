#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>

#include "hashtable.h"

typedef struct RedisDatabase {
    pthread_mutex_t mutex;
    HashTable *kv_store;    /* char* key -> char* value                */
    HashTable *list_store;  /* char* key -> StrList*                   */
    HashTable *hash_store;  /* char* key -> HashTable* (char*->char*)  */
    HashTable *expiry_map;  /* char* key -> time_t* (absolute deadline) */
} RedisDatabase;

/* Process-wide singleton, mirroring RedisDatabase::getInstance(). Call
 * once at startup; safe to call repeatedly (returns the same instance). */
RedisDatabase *redis_database_get_instance(void);

/* Releases all resources held by the singleton. Only meant to be
 * called once, during a clean process shutdown. */
void redis_database_shutdown(void);

/* ---- Common commands ---- */
bool rdb_flush_all(RedisDatabase *db);

/* ---- Key/Value operations ---- */
void rdb_set(RedisDatabase *db, const char *key, const char *value);
/* On success, *out_value is a malloc'd copy the caller must free. */
bool rdb_get(RedisDatabase *db, const char *key, char **out_value);
/* Returns a malloc'd array of malloc'd strings (*out_count entries);
 * caller frees each string and then the array. */
char **rdb_keys(RedisDatabase *db, size_t *out_count);
/* Returns a static string: "string", "list", "hash", or "none". */
const char *rdb_type(RedisDatabase *db, const char *key);
bool rdb_del(RedisDatabase *db, const char *key);
bool rdb_expire(RedisDatabase *db, const char *key, int seconds);
bool rdb_rename(RedisDatabase *db, const char *old_key, const char *new_key);

/* ---- List operations ---- */
char **rdb_lget(RedisDatabase *db, const char *key, size_t *out_count);
ssize_t rdb_llen(RedisDatabase *db, const char *key);
void rdb_lpush(RedisDatabase *db, const char *key, const char *value);
void rdb_rpush(RedisDatabase *db, const char *key, const char *value);
bool rdb_lpop(RedisDatabase *db, const char *key, char **out_value);
bool rdb_rpop(RedisDatabase *db, const char *key, char **out_value);
int rdb_lrem(RedisDatabase *db, const char *key, int count, const char *value);
bool rdb_lindex(RedisDatabase *db, const char *key, int index, char **out_value);
bool rdb_lset(RedisDatabase *db, const char *key, int index, const char *value);

/* ---- Hash operations ---- */
bool rdb_hset(RedisDatabase *db, const char *key, const char *field, const char *value);
bool rdb_hget(RedisDatabase *db, const char *key, const char *field, char **out_value);
bool rdb_hexists(RedisDatabase *db, const char *key, const char *field);
bool rdb_hdel(RedisDatabase *db, const char *key, const char *field);
/* Parallel arrays of fields/values, *out_count entries each. */
bool rdb_hgetall(RedisDatabase *db, const char *key, char ***out_fields, char ***out_values, size_t *out_count);
char **rdb_hkeys(RedisDatabase *db, const char *key, size_t *out_count);
char **rdb_hvals(RedisDatabase *db, const char *key, size_t *out_count);
ssize_t rdb_hlen(RedisDatabase *db, const char *key);
bool rdb_hmset(RedisDatabase *db, const char *key, const char **fields, const char **values, size_t count);

/* ---- Persistence ---- */
bool rdb_dump(RedisDatabase *db, const char *filename);
bool rdb_load(RedisDatabase *db, const char *filename);

#endif /* REDIS_DATABASE_H */
