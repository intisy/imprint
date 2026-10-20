#ifndef IMPRINT_OPTIONS_H
#define IMPRINT_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct named_option {
    const char *name;
    const char *value;
} named_option;

/**
 * Reads "--name value" pairs into the matching option's value. False, with the offending word in *problem_word,
 * for an unknown option or one without a value.
 */
bool options_read(const char *const *words, size_t count, named_option *options, size_t option_count,
                  const char **problem_word);

#endif
