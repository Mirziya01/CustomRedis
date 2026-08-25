#include "../include/strlist.h"

#include <stdlib.h>
#include <string.h>

#define STRLIST_INITIAL_CAP 8

StrList *strlist_create(void) {
    StrList *list = malloc(sizeof(StrList));
    list->items = malloc(sizeof(char *) * STRLIST_INITIAL_CAP);
    list->count = 0;
    list->cap = STRLIST_INITIAL_CAP;
    return list;
}

void strlist_free(StrList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    free(list);
}

static void strlist_ensure_cap(StrList *list, size_t needed) {
    if (needed <= list->cap) return;
    size_t new_cap = list->cap * 2;
    while (new_cap < needed) new_cap *= 2;
    list->items = realloc(list->items, sizeof(char *) * new_cap);
    list->cap = new_cap;
}

void strlist_push_back(StrList *list, const char *value) {
    strlist_ensure_cap(list, list->count + 1);
    list->items[list->count++] = strdup(value);
}

void strlist_push_front(StrList *list, const char *value) {
    strlist_ensure_cap(list, list->count + 1);
    memmove(&list->items[1], &list->items[0], sizeof(char *) * list->count);
    list->items[0] = strdup(value);
    list->count++;
}

int strlist_pop_front(StrList *list, char **out_value) {
    if (list->count == 0) return 0;
    *out_value = list->items[0];
    memmove(&list->items[0], &list->items[1], sizeof(char *) * (list->count - 1));
    list->count--;
    return 1;
}

int strlist_pop_back(StrList *list, char **out_value) {
    if (list->count == 0) return 0;
    *out_value = list->items[list->count - 1];
    list->count--;
    return 1;
}

void strlist_remove_at(StrList *list, size_t idx) {
    if (idx >= list->count) return;
    free(list->items[idx]);
    memmove(&list->items[idx], &list->items[idx + 1], sizeof(char *) * (list->count - idx - 1));
    list->count--;
}
