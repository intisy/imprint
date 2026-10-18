#ifndef IMPRINT_IDENTIFIER_H
#define IMPRINT_IDENTIFIER_H

#include <stdbool.h>

extern const char *const IDENTIFIER_RULE;
extern const char *const SETUP_NAME_RULE;

bool identifier_is_valid(const char *text);
bool setup_name_is_valid(const char *text);
bool identifier_has_word(const char *text, const char *word);

#endif
