#include "text.h"
#include "utest.h"

#include <stdlib.h>

UTEST(text, duplicate_copies_the_text) {
    char *copy = text_duplicate("imprint");
    ASSERT_STREQ("imprint", copy);
    free(copy);
}

UTEST(text, format_builds_the_text) {
    char *formatted = text_format("%s-%d", "setup", 2);
    ASSERT_STREQ("setup-2", formatted);
    free(formatted);
}

UTEST(text, path_join_adds_exactly_one_separator) {
    char *plain = path_join("data", "setups");
    char *trailing = path_join("data/", "setups");
    char *windows = path_join("C:\\Some One\\imprint\\", "data");
    ASSERT_STREQ("data/setups", plain);
    ASSERT_STREQ("data/setups", trailing);
    ASSERT_STREQ("C:\\Some One\\imprint\\data", windows);
    free(plain);
    free(trailing);
    free(windows);
}

UTEST(text, ends_with_compares_the_tail) {
    ASSERT_TRUE(text_ends_with("base.toml", ".toml"));
    ASSERT_FALSE(text_ends_with("toml", ".toml"));
    ASSERT_FALSE(text_ends_with("base.json", ".toml"));
}

UTEST(text, without_suffix_drops_the_tail) {
    char *stem = text_without_suffix("base.toml", ".toml");
    char *unchanged = text_without_suffix("base.json", ".toml");
    ASSERT_STREQ("base", stem);
    ASSERT_STREQ("base.json", unchanged);
    free(stem);
    free(unchanged);
}
