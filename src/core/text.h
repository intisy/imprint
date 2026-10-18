#ifndef IMPRINT_TEXT_H
#define IMPRINT_TEXT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__MINGW32__)
#define IMPRINT_PRINTF(format_index, first_argument) \
    __attribute__((format(gnu_printf, format_index, first_argument)))
#elif defined(__GNUC__)
#define IMPRINT_PRINTF(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define IMPRINT_PRINTF(format_index, first_argument)
#endif

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/** @implNote A command-line tool can do nothing useful once allocation fails, so it aborts. */
void *memory_allocate(size_t size);
void *memory_reallocate(void *memory, size_t size);

char *text_duplicate(const char *text);
char *text_format(const char *format, ...) IMPRINT_PRINTF(1, 2);
char *text_format_arguments(const char *format, va_list arguments);
char *path_join(const char *directory, const char *name);
bool text_ends_with(const char *text, const char *suffix);
char *text_without_suffix(const char *text, const char *suffix);

#endif
