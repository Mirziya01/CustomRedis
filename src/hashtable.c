#include "../include/hashtable.h"

#include <stdlib.h>
#include <string.h>

static unsigned long fnv1a_hash(const char *s) {
    unsigned long hash = 2166136261UL;
    while (*s) {
        hash ^= (unsigned char)(*s++);
        hash *= 16777619UL;
    }
    return hash;
}

static void ht_resize(HashTable *ht, size_t new_bucket_count);

HashTable *ht_create(size_t initial_buckets) {
    if (initial_buckets == 0) initial_buckets = 16;
    HashTable *ht = malloc(sizeof(HashTable));
    if (!ht) return NULL;
    ht->buckets = calloc(initial_buckets, sizeof(HashEntry *));
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }
    ht->bucket_count = initial_buckets;
    ht->size = 0;
    return ht;
}

void ht_clear(HashTable *ht, ht_free_fn free_value) {
    if (!ht) return;
    for (size_t i = 0; i < ht->bucket_count; i++) {
        HashEntry *e = ht->buckets[i];
        while (e) {
            HashEntry *next = e->next;
            if (free_value) free_value(e->value);
            free(e->key);
            free(e);
            e = next;
        }
        ht->buckets[i] = NULL;
    }
    ht->size = 0;
}

void ht_destroy(HashTable *ht, ht_free_fn free_value) {
    if (!ht) return;
    ht_clear(ht, free_value);
    free(ht->buckets);
    free(ht);
}

static void ht_resize(HashTable *ht, size_t new_bucket_count) {
    HashEntry **new_buckets = calloc(new_bucket_count, sizeof(HashEntry *));
    if (!new_buckets) return; /* best effort; keep old table on OOM */

    for (size_t i = 0; i < ht->bucket_count; i++) {
        HashEntry *e = ht->buckets[i];
        while (e) {
            HashEntry *next = e->next;
            size_t idx = fnv1a_hash(e->key) % new_bucket_count;
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }
    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->bucket_count = new_bucket_count;
}

void ht_set(HashTable *ht, const char *key, void *value, ht_free_fn free_old) {
    if (!ht || !key) return;

    /* Grow when the load factor gets high, to keep lookups O(1)-ish. */
    if (ht->size + 1 > ht->bucket_count * 3 / 2) {
        ht_resize(ht, ht->bucket_count * 2);
    }

    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    for (HashEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (free_old) free_old(e->value);
            e->value = value;
            return;
        }
    }

    HashEntry *entry = malloc(sizeof(HashEntry));
    entry->key = strdup(key);
    entry->value = value;
    entry->next = ht->buckets[idx];
    ht->buckets[idx] = entry;
    ht->size++;
}

void *ht_get(HashTable *ht, const char *key) {
    if (!ht || !key) return NULL;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    for (HashEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->value;
    }
    return NULL;
}

int ht_exists(HashTable *ht, const char *key) {
    if (!ht || !key) return 0;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    for (HashEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return 1;
    }
    return 0;
}

int ht_del(HashTable *ht, const char *key, ht_free_fn free_value) {
    if (!ht || !key) return 0;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    HashEntry *prev = NULL;
    HashEntry *e = ht->buckets[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else ht->buckets[idx] = e->next;
            if (free_value) free_value(e->value);
            free(e->key);
            free(e);
            ht->size--;
            return 1;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}

int ht_rename(HashTable *ht, const char *old_key, const char *new_key, ht_free_fn free_old) {
    if (!ht || !old_key || !new_key) return 0;
    if (strcmp(old_key, new_key) == 0) return ht_exists(ht, old_key);

    size_t old_idx = fnv1a_hash(old_key) % ht->bucket_count;
    HashEntry *prev = NULL;
    HashEntry *e = ht->buckets[old_idx];
    while (e) {
        if (strcmp(e->key, old_key) == 0) break;
        prev = e;
        e = e->next;
    }
    if (!e) return 0;

    /* unlink from old bucket */
    if (prev) prev->next = e->next;
    else ht->buckets[old_idx] = e->next;
    ht->size--;

    void *value = e->value;
    free(e->key);
    free(e);

    ht_set(ht, new_key, value, free_old);
    return 1;
}

void ht_foreach(HashTable *ht, ht_foreach_fn fn, void *user_data) {
    if (!ht || !fn) return;
    for (size_t i = 0; i < ht->bucket_count; i++) {
        for (HashEntry *e = ht->buckets[i]; e; e = e->next) {
            fn(e->key, e->value, user_data);
        }
    }
}

size_t ht_size(const HashTable *ht) {
    return ht ? ht->size : 0;
}
