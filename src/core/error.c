#include "error.h"

#include <stdlib.h>
#include <string.h>

void error_list_init(error_list *errors) {
    errors->items = NULL;
    errors->count = 0;
    errors->capacity = 0;
}

void error_list_free(error_list *errors) {
    for (size_t index = 0; index < errors->count; index++) {
        free(errors->items[index].file);
        free(errors->items[index].message);
    }
    free(errors->items);
    error_list_init(errors);
}

void error_push_arguments(error_list *errors, const char *file, int line, const char *format, va_list arguments) {
    if (errors->count == errors->capacity) {
        errors->capacity = errors->capacity == 0 ? 4 : errors->capacity * 2;
        errors->items = memory_reallocate(errors->items, errors->capacity * sizeof *errors->items);
    }
    imprint_error *error = &errors->items[errors->count++];
    error->file = file == NULL ? NULL : text_duplicate(file);
    error->line = line;
    error->message = text_format_arguments(format, arguments);
}

void error_push(error_list *errors, const char *file, int line, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    error_push_arguments(errors, file, line, format, arguments);
    va_end(arguments);
}

char *error_format(const imprint_error *error) {
    if (error->file == NULL) {
        return text_duplicate(error->message);
    }
    if (error->line > 0) {
        return text_format("%s:%d: %s", error->file, error->line, error->message);
    }
    return text_format("%s: %s", error->file, error->message);
}

void error_list_print(const error_list *errors, FILE *stream) {
    for (size_t index = 0; index < errors->count; index++) {
        char *formatted = error_format(&errors->items[index]);
        fprintf(stream, "%s\n", formatted);
        free(formatted);
    }
}

bool error_list_mentions(const error_list *errors, const char *fragment) {
    for (size_t index = 0; index < errors->count; index++) {
        char *formatted = error_format(&errors->items[index]);
        bool found = strstr(formatted, fragment) != NULL;
        free(formatted);
        if (found) {
            return true;
        }
    }
    return false;
}
