#include "identifier.h"
#include "utest.h"

UTEST(identifier, accepts_project_names) {
    ASSERT_TRUE(identifier_is_valid("steam"));
    ASSERT_TRUE(identifier_is_valid("visual-studio-code"));
    ASSERT_TRUE(identifier_is_valid("jetbrains-toolbox"));
    ASSERT_TRUE(identifier_is_valid("i3"));
    ASSERT_TRUE(identifier_is_valid("7zip"));
}

UTEST(identifier, refuses_anything_else) {
    ASSERT_FALSE(identifier_is_valid(""));
    ASSERT_FALSE(identifier_is_valid("Steam"));
    ASSERT_FALSE(identifier_is_valid("jetbrains_toolbox"));
    ASSERT_FALSE(identifier_is_valid("-steam"));
    ASSERT_FALSE(identifier_is_valid("steam-"));
    ASSERT_FALSE(identifier_is_valid("visual--studio"));
    ASSERT_FALSE(identifier_is_valid("with space"));
    ASSERT_FALSE(identifier_is_valid("../catalog"));
    ASSERT_FALSE(identifier_is_valid("a1234567890123456789012345678901234567890123456789012345678901234"));
}

UTEST(identifier, setup_names_are_a_purpose_and_an_optional_variant) {
    ASSERT_TRUE(setup_name_is_valid("base"));
    ASSERT_TRUE(setup_name_is_valid("workstation"));
    ASSERT_TRUE(setup_name_is_valid("storage-worker"));
    ASSERT_FALSE(setup_name_is_valid("dev-workstation-gpu"));
    ASSERT_FALSE(setup_name_is_valid("Workstation"));
    ASSERT_FALSE(setup_name_is_valid("my setup"));
    ASSERT_FALSE(setup_name_is_valid("2nd-setup"));
    ASSERT_FALSE(setup_name_is_valid("storage_worker"));
    ASSERT_FALSE(setup_name_is_valid(""));
}

UTEST(identifier, has_word_compares_whole_words) {
    ASSERT_TRUE(identifier_has_word("workstation-arch", "arch"));
    ASSERT_TRUE(identifier_has_word("arch", "arch"));
    ASSERT_FALSE(identifier_has_word("archive", "arch"));
    ASSERT_FALSE(identifier_has_word("search-tool", "arch"));
}
