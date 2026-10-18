#include "string_list.h"

#include "text.h"

#include <stdlib.h>
#include <string.h>

void string_list_init(string_list *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void string_list_free(string_list *list) {
    for (size_t index = 0; index < list->count; index++) {
        free(list->items[index]);
    }
    free(list->items);
    string_list_init(list);
}

void string_list_add(string_list *list, const char *text) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = memory_reallocate(list->items, list->capacity * sizeof *list->items);
    }
    list->items[list->count++] = text_duplicate(text);
}

bool string_list_contains(const string_list *list, const char *text) {
    for (size_t index = 0; index < list->count; index++) {
        if (strcmp(list->items[index], text) == 0) {
            return true;
        }
    }
    return false;
}

bool string_list_add_unique(string_list *list, const char *text) {
    if (string_list_contains(list, text)) {
        return false;
    }
    string_list_add(list, text);
    return true;
}

void string_list_remove_all(string_list *list, const char *text) {
    size_t kept = 0;
    for (size_t index = 0; index < list->count; index++) {
        if (strcmp(list->items[index], text) == 0) {
            free(list->items[index]);
        } else {
            list->items[kept++] = list->items[index];
        }
    }
    list->count = kept;
}

void string_list_reverse(string_list *list) {
    for (size_t front = 0, back = list->count; front + 1 < back; front++, back--) {
        char *swapped = list->items[front];
        list->items[front] = list->items[back - 1];
        list->items[back - 1] = swapped;
    }
}

static int compare_texts(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

void string_list_sort(string_list *list) {
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof *list->items, compare_texts);
    }
}

char *string_list_join(const string_list *list, const char *separator) {
    size_t length = 1;
    for (size_t index = 0; index < list->count; index++) {
        length += strlen(list->items[index]) + (index > 0 ? strlen(separator) : 0);
    }
    char *joined = memory_allocate(length);
    joined[0] = '\0';
    for (size_t index = 0; index < list->count; index++) {
        if (index > 0) {
            strcat(joined, separator);
        }
        strcat(joined, list->items[index]);
    }
    return joined;
}
