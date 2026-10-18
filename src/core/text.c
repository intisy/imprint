#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void out_of_memory(void) {
    fputs("imprint: out of memory\n", stderr);
    abort();
}

void *memory_allocate(size_t size) {
    void *memory = malloc(size == 0 ? 1 : size);
    if (memory == NULL) {
        out_of_memory();
    }
    return memory;
}

void *memory_reallocate(void *memory, size_t size) {
    void *resized = realloc(memory, size == 0 ? 1 : size);
    if (resized == NULL) {
        out_of_memory();
    }
    return resized;
}

char *text_duplicate(const char *text) {
    size_t length = strlen(text);
    char *copy = memory_allocate(length + 1);
    memcpy(copy, text, length + 1);
    return copy;
}

char *text_format_arguments(const char *format, va_list arguments) {
    va_list measuring;
    va_copy(measuring, arguments);
    int length = vsnprintf(NULL, 0, format, measuring);
    va_end(measuring);
    if (length < 0) {
        return text_duplicate("");
    }
    char *formatted = memory_allocate((size_t)length + 1);
    vsnprintf(formatted, (size_t)length + 1, format, arguments);
    return formatted;
}

char *text_format(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *formatted = text_format_arguments(format, arguments);
    va_end(arguments);
    return formatted;
}

char *path_join(const char *directory, const char *name) {
    size_t length = strlen(directory);
    bool ends_in_separator = length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\');
    if (ends_in_separator) {
        return text_format("%s%s", directory, name);
    }
    return text_format("%s/%s", directory, name);
}

bool text_ends_with(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length > suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

char *text_without_suffix(const char *text, const char *suffix) {
    char *copy = text_duplicate(text);
    if (text_ends_with(text, suffix)) {
        copy[strlen(text) - strlen(suffix)] = '\0';
    }
    return copy;
}
