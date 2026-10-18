#ifndef IMPRINT_ERROR_H
#define IMPRINT_ERROR_H

#include "text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct imprint_error {
    char *file;
    int line;
    char *message;
} imprint_error;

typedef struct error_list {
    imprint_error *items;
    size_t count;
    size_t capacity;
} error_list;

void error_list_init(error_list *errors);
void error_list_free(error_list *errors);
void error_push(error_list *errors, const char *file, int line, const char *format, ...) IMPRINT_PRINTF(4, 5);
void error_push_arguments(error_list *errors, const char *file, int line, const char *format, va_list arguments);
char *error_format(const imprint_error *error);
void error_list_print(const error_list *errors, FILE *stream);
bool error_list_mentions(const error_list *errors, const char *fragment);

#endif
