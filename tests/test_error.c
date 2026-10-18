#include "error.h"
#include "utest.h"

#include <stdlib.h>

UTEST(error, format_with_file_and_line) {
    error_list errors;
    error_list_init(&errors);
    error_push(&errors, "base.toml", 3, "unknown key '%s'", "distr");
    char *formatted = error_format(&errors.items[0]);
    ASSERT_STREQ("base.toml:3: unknown key 'distr'", formatted);
    free(formatted);
    error_list_free(&errors);
}

UTEST(error, format_without_line_or_file) {
    error_list errors;
    error_list_init(&errors);
    error_push(&errors, "base.toml", 0, "cannot read");
    error_push(&errors, NULL, 0, "git is not on PATH");
    char *with_file = error_format(&errors.items[0]);
    char *bare = error_format(&errors.items[1]);
    ASSERT_STREQ("base.toml: cannot read", with_file);
    ASSERT_STREQ("git is not on PATH", bare);
    free(with_file);
    free(bare);
    error_list_free(&errors);
}

UTEST(error, list_grows_and_keeps_order) {
    error_list errors;
    error_list_init(&errors);
    for (int index = 0; index < 10; index++) {
        error_push(&errors, NULL, 0, "problem %d", index);
    }
    ASSERT_EQ((size_t)10, errors.count);
    ASSERT_STREQ("problem 9", errors.items[9].message);
    ASSERT_TRUE(error_list_mentions(&errors, "problem 4"));
    ASSERT_FALSE(error_list_mentions(&errors, "problem 10"));
    error_list_free(&errors);
    ASSERT_EQ((size_t)0, errors.count);
}
