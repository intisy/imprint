#include "string_list.h"
#include "utest.h"

#include <stdlib.h>

UTEST(string_list, add_unique_refuses_a_duplicate) {
    string_list list;
    string_list_init(&list);
    ASSERT_TRUE(string_list_add_unique(&list, "git"));
    ASSERT_FALSE(string_list_add_unique(&list, "git"));
    ASSERT_EQ((size_t)1, list.count);
    ASSERT_TRUE(string_list_contains(&list, "git"));
    ASSERT_FALSE(string_list_contains(&list, "steam"));
    string_list_free(&list);
}

UTEST(string_list, sort_and_reverse) {
    string_list list;
    string_list_init(&list);
    string_list_add(&list, "workstation");
    string_list_add(&list, "base");
    string_list_add(&list, "example");
    string_list_sort(&list);
    char *sorted = string_list_join(&list, " ");
    string_list_reverse(&list);
    char *reversed = string_list_join(&list, " ");
    ASSERT_STREQ("base example workstation", sorted);
    ASSERT_STREQ("workstation example base", reversed);
    free(sorted);
    free(reversed);
    string_list_free(&list);
}

UTEST(string_list, join_of_nothing_is_empty) {
    string_list list;
    string_list_init(&list);
    char *joined = string_list_join(&list, ", ");
    ASSERT_STREQ("", joined);
    free(joined);
    string_list_free(&list);
}
