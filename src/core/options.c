#include "options.h"

#include <string.h>

static named_option *find_option(const char *word, named_option *options, size_t option_count) {
    for (size_t index = 0; index < option_count; index++) {
        if (strcmp(word, options[index].name) == 0) {
            return &options[index];
        }
    }
    return NULL;
}

bool options_read(const char *const *words, size_t count, named_option *options, size_t option_count,
                  const char **problem_word) {
    for (size_t index = 0; index < count; index += 2) {
        named_option *option = find_option(words[index], options, option_count);
        if (option == NULL || index + 1 >= count) {
            *problem_word = words[index];
            return false;
        }
        option->value = words[index + 1];
    }
    return true;
}
