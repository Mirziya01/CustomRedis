#ifndef STRLIST_H
#define STRLIST_H

#include <stddef.h>

/* A simple growable array of owned (malloc'd) strings.*/
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} StrList;

StrList *strlist_create(void);
void strlist_free(StrList *list);

void strlist_push_back(StrList *list, const char *value);
void strlist_push_front(StrList *list, const char *value);

/* Removes and returns (via out_value, caller frees) the front/back
 * element. Returns 1 on success, 0 if the list was empty. */
int strlist_pop_front(StrList *list, char **out_value);
int strlist_pop_back(StrList *list, char **out_value);

/* Removes element at `idx` (0-based, must be in range). */
void strlist_remove_at(StrList *list, size_t idx);

#endif /* STRLIST_H */
