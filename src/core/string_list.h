#ifndef IMPRINT_STRING_LIST_H
#define IMPRINT_STRING_LIST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct string_list {
    char **items;
    size_t count;
    size_t capacity;
} string_list;

void string_list_init(string_list *list);
void string_list_free(string_list *list);
void string_list_add(string_list *list, const char *text);
bool string_list_add_unique(string_list *list, const char *text);
bool string_list_contains(const string_list *list, const char *text);
void string_list_reverse(string_list *list);
void string_list_sort(string_list *list);
char *string_list_join(const string_list *list, const char *separator);

#endif
