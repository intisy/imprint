#include "identifier.h"

#include <stddef.h>
#include <string.h>

#define IDENTIFIER_MAXIMUM_LENGTH 64
#define STRINGIFIED(value) STRINGIFIED_VALUE(value)
#define STRINGIFIED_VALUE(value) #value
#define MAXIMUM_LENGTH_TEXT STRINGIFIED(IDENTIFIER_MAXIMUM_LENGTH)

const char *const IDENTIFIER_RULE =
    "lowercase letters and digits in words joined by single hyphens, at most " MAXIMUM_LENGTH_TEXT " characters";
const char *const SETUP_NAME_RULE =
    "<purpose>[-<variant>]: one or two words of lowercase letters and digits joined by a hyphen, starting with a "
    "letter, at most " MAXIMUM_LENGTH_TEXT " characters";

static bool is_word_character(char character) {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
}

static size_t word_count(const char *text) {
    size_t length = strlen(text);
    if (length == 0 || length > IDENTIFIER_MAXIMUM_LENGTH) {
        return 0;
    }
    size_t words = 1;
    bool after_hyphen = true;
    for (const char *character = text; *character != '\0'; character++) {
        if (*character == '-') {
            if (after_hyphen) {
                return 0;
            }
            after_hyphen = true;
            words++;
            continue;
        }
        if (!is_word_character(*character)) {
            return 0;
        }
        after_hyphen = false;
    }
    return after_hyphen ? 0 : words;
}

bool identifier_is_valid(const char *text) {
    return word_count(text) > 0;
}

bool setup_name_is_valid(const char *text) {
    size_t words = word_count(text);
    return words >= 1 && words <= 2 && text[0] >= 'a' && text[0] <= 'z';
}

bool identifier_has_word(const char *text, const char *word) {
    size_t word_length = strlen(word);
    const char *start = text;
    for (;;) {
        const char *end = strchr(start, '-');
        size_t length = end == NULL ? strlen(start) : (size_t)(end - start);
        if (length == word_length && strncmp(start, word, length) == 0) {
            return true;
        }
        if (end == NULL) {
            return false;
        }
        start = end + 1;
    }
}
