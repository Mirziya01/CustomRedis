#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>

/*
 * A generic string-keyed hash table. Values are stored as `void*`
 * and the table takes ownership of both the (duplicated) key and
 * whatever value is inserted -- callers supply a `free_value`
 * callback so the table knows how to release entries on
 * delete/clear/destroy.
 *
 * Each bucket holds a red-black tree (ordered by strcmp on the key)
 * rather than a plain list or an unbalanced BST. A red-black tree
 * keeps itself height-balanced after every insert and delete via
 * rotations and recoloring, so a bucket's lookup/insert/delete cost
 * is O(log n) *guaranteed*, regardless of insertion order -- unlike a
 * plain BST, which can degrade toward a linked list if keys happen
 * to arrive in sorted order. This is the same structure used inside
 * Java's TreeMap and the Linux kernel scheduler, for the same reason:
 * predictable worst-case behavior under a mix of reads and writes.
 *
 * Implementation note: each HashTable owns a single shared sentinel
 * node (`nil`) representing "no child," used by every bucket's tree.
 * This is the standard CLRS-style red-black tree technique -- it
 * lets every node have real left/right/parent pointers (even at the
 * edges of the tree) without special-casing NULL everywhere.
 *
 * This is NOT thread-safe on its own; callers (RedisDatabase) are
 * responsible for locking around it.
 */

typedef void (*ht_free_fn)(void *value);
typedef void (*ht_foreach_fn)(const char *key, void *value, void *user_data);

typedef enum { RB_RED, RB_BLACK } RbColor;

typedef struct HashEntry {
    char *key;
    void *value;
    struct HashEntry *left;
    struct HashEntry *right;
    struct HashEntry *parent;
    RbColor color;
} HashEntry;

typedef struct HashTable {
    HashEntry **buckets; /* array of red-black tree roots, one per bucket */
    HashEntry *nil;      /* shared sentinel; represents "no child" everywhere */
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

/* Visits every entry in ascending key order (in-order traversal per
 * bucket, buckets visited in index order). Safe to use for
 * reading/copying; do not insert or delete from `ht` while a
 * ht_foreach call over it is in progress. */
void ht_foreach(HashTable *ht, ht_foreach_fn fn, void *user_data);

size_t ht_size(const HashTable *ht);

#endif /* HASHTABLE_H */
