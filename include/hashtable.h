#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>

/*
 * A small generic string-keyed hash table with separate chaining.
 * Values are stored as `void*` and the table itself takes ownership
 * of both the (duplicated) key and whatever value is inserted -- the
 * caller supplies a `free_value` callback so the table knows how to
 * release entries on delete/clear/destroy.
 *
 * It is NOT thread-safe on its own; callers
 * (RedisDatabase) are responsible for locking around it.
 */

typedef void (*ht_free_fn)(void *value);
typedef void (*ht_foreach_fn)(const char *key, void *value, void *user_data);

typedef struct HashEntry {
    char *key;
    void *value;
    struct HashEntry *next;
} HashEntry;

typedef struct HashTable {
    HashEntry **buckets;
    size_t bucket_count;
    size_t size;
} HashTable;

HashTable *ht_create(size_t initial_buckets);
void ht_destroy(HashTable *ht, ht_free_fn free_value);
void ht_clear(HashTable *ht, ht_free_fn free_value);

/* Inserts or overwrites `key`. If a value already existed and
 * `free_old` is non-NULL, the previous value is freed with it. */
void ht_set(HashTable *ht, const char *key, void *value, ht_free_fn free_old);

void *ht_get(HashTable *ht, const char *key);
int ht_exists(HashTable *ht, const char *key);

/* Removes `key`, freeing its value with `free_value` if non-NULL.
 * Returns 1 if a key was removed, 0 otherwise. */
int ht_del(HashTable *ht, const char *key, ht_free_fn free_value);

/* Moves the entry at old_key to new_key (overwriting any existing
 * entry at new_key, which is freed with `free_old`). Returns 1 on
 * success, 0 if old_key did not exist. */
int ht_rename(HashTable *ht, const char *old_key, const char *new_key, ht_free_fn free_old);

void ht_foreach(HashTable *ht, ht_foreach_fn fn, void *user_data);
size_t ht_size(const HashTable *ht);

#endif /* HASHTABLE_H */
