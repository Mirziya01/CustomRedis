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

/* ---- per-bucket BST helpers --------------------------------------
 * Each bucket is the root of its own small binary search tree,
 * ordered by strcmp() on the key. These operate on a `HashEntry**`
 * (a pointer to whichever pointer currently holds "the root of this
 * subtree") so that inserting/removing the actual root of a bucket
 * is no different from any other node -- no special-casing needed.
 * ------------------------------------------------------------------ */

static HashEntry *bst_find(HashEntry *node, const char *key) {
    while (node) {
        int cmp = strcmp(key, node->key);
        if (cmp == 0) return node;
        node = (cmp < 0) ? node->left : node->right;
    }
    return NULL;
}

/* Inserts `entry` as a new leaf. Caller must already know `entry`'s
 * key is not present in this tree (ht_set checks via bst_find first). */
static void bst_insert_new(HashEntry **root, HashEntry *entry) {
    HashEntry **cur = root;
    while (*cur) {
        int cmp = strcmp(entry->key, (*cur)->key);
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }
    *cur = entry;
}

/* Removes the node matching `key` from the tree rooted at *root and
 * returns it, unlinked (left/right cleared). Returns NULL if not
 * found. Handles the standard three BST-delete cases, including the
 * two-children case via in-order successor. */
static HashEntry *bst_remove(HashEntry **root, const char *key) {
    HashEntry **cur = root;
    while (*cur) {
        int cmp = strcmp(key, (*cur)->key);
        if (cmp == 0) break;
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }
    if (!*cur) return NULL;

    HashEntry *target = *cur;

    if (target->left && target->right) {
        /* Two children: splice in the in-order successor (the
         * leftmost node of the right subtree, which by definition
         * has no left child of its own). */
        HashEntry **succ_ptr = &target->right;
        while ((*succ_ptr)->left) succ_ptr = &(*succ_ptr)->left;
        HashEntry *succ = *succ_ptr;

        *succ_ptr = succ->right; /* unlink succ from its old spot */
        succ->left = target->left;
        succ->right = target->right;
        *cur = succ;
    } else {
        /* Zero or one child: promote the child (or NULL) directly. */
        *cur = target->left ? target->left : target->right;
    }

    target->left = target->right = NULL;
    return target;
}

static void bst_foreach(HashEntry *node, ht_foreach_fn fn, void *user_data) {
    if (!node) return;
    bst_foreach(node->left, fn, user_data);
    fn(node->key, node->value, user_data);
    bst_foreach(node->right, fn, user_data);
}

static void bst_destroy(HashEntry *node, ht_free_fn free_value) {
    if (!node) return;
    bst_destroy(node->left, free_value);
    bst_destroy(node->right, free_value);
    if (free_value) free_value(node->value);
    free(node->key);
    free(node);
}

/* Re-homes every node in the subtree rooted at `node` into
 * `new_buckets` (sized `new_bucket_count`), rehashing each key.
 * Reuses the existing HashEntry allocations rather than
 * malloc'ing fresh ones. */
static void bst_rehash_into(HashEntry *node, HashEntry **new_buckets, size_t new_bucket_count) {
    if (!node) return;
    HashEntry *left = node->left;
    HashEntry *right = node->right;
    node->left = node->right = NULL;

    size_t idx = fnv1a_hash(node->key) % new_bucket_count;
    bst_insert_new(&new_buckets[idx], node);

    bst_rehash_into(left, new_buckets, new_bucket_count);
    bst_rehash_into(right, new_buckets, new_bucket_count);
}

/* ---- HashTable ---------------------------------------------------- */

static void ht_resize(HashTable *ht, size_t new_bucket_count) {
    HashEntry **new_buckets = calloc(new_bucket_count, sizeof(HashEntry *));
    if (!new_buckets) return; /* best effort; keep old table on OOM */

    for (size_t i = 0; i < ht->bucket_count; i++) {
        bst_rehash_into(ht->buckets[i], new_buckets, new_bucket_count);
    }
    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->bucket_count = new_bucket_count;
}

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
        bst_destroy(ht->buckets[i], free_value);
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

void ht_set(HashTable *ht, const char *key, void *value, ht_free_fn free_old) {
    if (!ht || !key) return;

    /* Grow when the load factor gets high, to keep each bucket's
     * tree shallow. */
    if (ht->size + 1 > ht->bucket_count * 3 / 2) {
        ht_resize(ht, ht->bucket_count * 2);
    }

    size_t idx = fnv1a_hash(key) % ht->bucket_count;

    HashEntry *existing = bst_find(ht->buckets[idx], key);
    if (existing) {
        if (free_old) free_old(existing->value);
        existing->value = value;
        return;
    }

    HashEntry *entry = malloc(sizeof(HashEntry));
    entry->key = strdup(key);
    entry->value = value;
    entry->left = NULL;
    entry->right = NULL;
    bst_insert_new(&ht->buckets[idx], entry);
    ht->size++;
}

void *ht_get(HashTable *ht, const char *key) {
    if (!ht || !key) return NULL;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    HashEntry *e = bst_find(ht->buckets[idx], key);
    return e ? e->value : NULL;
}

int ht_exists(HashTable *ht, const char *key) {
    if (!ht || !key) return 0;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;
    return bst_find(ht->buckets[idx], key) != NULL;
}

int ht_del(HashTable *ht, const char *key, ht_free_fn free_value) {
    if (!ht || !key) return 0;
    size_t idx = fnv1a_hash(key) % ht->bucket_count;

    HashEntry *removed = bst_remove(&ht->buckets[idx], key);
    if (!removed) return 0;

    if (free_value) free_value(removed->value);
    free(removed->key);
    free(removed);
    ht->size--;
    return 1;
}

int ht_rename(HashTable *ht, const char *old_key, const char *new_key, ht_free_fn free_old) {
    if (!ht || !old_key || !new_key) return 0;
    if (strcmp(old_key, new_key) == 0) return ht_exists(ht, old_key);

    size_t old_idx = fnv1a_hash(old_key) % ht->bucket_count;
    HashEntry *removed = bst_remove(&ht->buckets[old_idx], old_key);
    if (!removed) return 0;
    ht->size--;

    void *value = removed->value;
    free(removed->key);
    free(removed);

    ht_set(ht, new_key, value, free_old);
    return 1;
}

void ht_foreach(HashTable *ht, ht_foreach_fn fn, void *user_data) {
    if (!ht || !fn) return;
    for (size_t i = 0; i < ht->bucket_count; i++) {
        bst_foreach(ht->buckets[i], fn, user_data);
    }
}

size_t ht_size(const HashTable *ht) {
    return ht ? ht->size : 0;
}
