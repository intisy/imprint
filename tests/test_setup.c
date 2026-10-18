#include "setup.h"
#include "utest.h"

#include <stdlib.h>

#define DATA IMPRINT_TEST_FIXTURES "/data"
#define BROKEN IMPRINT_TEST_FIXTURES "/broken/data"
#define CHAIN IMPRINT_TEST_FIXTURES "/chain/data"

static char *joined_list(const document_node *array) {
    string_list values;
    string_list_init(&values);
    for (size_t index = 0; index < array->child_count; index++) {
        string_list_add(&values, array->children[index]->string_value);
    }
    char *text = string_list_join(&values, ",");
    string_list_free(&values);
    return text;
}

static bool load_broken(const char *name, error_list *errors) {
    setup loaded;
    bool valid = setup_load(BROKEN, name, &loaded, errors);
    setup_free(&loaded);
    return valid;
}

UTEST(setup, list_returns_the_setups_sorted) {
    string_list names;
    string_list_init(&names);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(setup_list(DATA, &names, &errors));
    char *joined = string_list_join(&names, ",");
    ASSERT_STREQ("base,example,workstation", joined);
    free(joined);
    string_list_free(&names);
}

UTEST(setup, list_names_each_file_that_breaks_the_pattern_and_keeps_the_rest) {
    string_list names;
    string_list_init(&names);
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(setup_list(BROKEN, &names, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "Workstation.toml: 'Workstation' is not a setup name"));
    ASSERT_TRUE(error_list_mentions(&errors, "my setup.toml: 'my setup' is not a setup name"));
    ASSERT_TRUE(string_list_contains(&names, "loop-a"));
    ASSERT_FALSE(string_list_contains(&names, "Workstation"));
    string_list_free(&names);
    error_list_free(&errors);
}

UTEST(setup, list_without_a_checked_out_data_repo_says_how_to_fix_it) {
    string_list names;
    string_list_init(&names);
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(setup_list(IMPRINT_TEST_FIXTURES "/empty-root/data", &names, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "git submodule update --init"));
    string_list_free(&names);
    error_list_free(&errors);
}

UTEST(setup, load_merges_the_chain_and_applies_remove) {
    setup loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(setup_load(DATA, "workstation", &loaded, &errors));
    char *chain = string_list_join(&loaded.chain, ",");
    char *apps = joined_list(document_get(loaded.merged, "apps"));
    char *packages = joined_list(document_get(document_get(loaded.merged, "extra_packages"), "arch"));
    char *groups = joined_list(document_get(document_get(loaded.merged, "system"), "user_groups"));
    ASSERT_STREQ("base,workstation", chain);
    ASSERT_STREQ("git,firefox,steam,mangohud,visual-studio-code,obsidian,tailscale,local-tool", apps);
    ASSERT_STREQ("htop", packages);
    ASSERT_STREQ("video", groups);
    ASSERT_STREQ("lightdm", document_get(loaded.merged, "display_manager")->string_value);
    ASSERT_STREQ("Europe/Berlin", document_get(document_get(loaded.merged, "install"), "timezone")->string_value);
    ASSERT_STREQ("en_US.UTF-8", document_get(document_get(loaded.merged, "install"), "locale")->string_value);
    ASSERT_TRUE(document_get(loaded.merged, "extends") == NULL);
    ASSERT_TRUE(document_get(loaded.merged, "remove") == NULL);
    free(chain);
    free(apps);
    free(packages);
    free(groups);
    setup_free(&loaded);
    error_list_free(&errors);
}

UTEST(setup, a_chain_of_three_keeps_the_deepest_base_and_lets_the_child_win) {
    setup loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(setup_load(CHAIN, "chain-a", &loaded, &errors));
    char *chain = string_list_join(&loaded.chain, ",");
    char *apps = joined_list(document_get(loaded.merged, "apps"));
    ASSERT_STREQ("chain-c,chain-b,chain-a", chain);
    ASSERT_STREQ("git,vim,nano", apps);
    ASSERT_STREQ("arch", document_get(loaded.merged, "distro")->string_value);
    ASSERT_STREQ("sddm", document_get(loaded.merged, "display_manager")->string_value);
    ASSERT_STREQ("en_US.UTF-8", document_get(document_get(loaded.merged, "install"), "locale")->string_value);
    ASSERT_TRUE(document_get(loaded.merged, "extends") == NULL);
    free(chain);
    free(apps);
    setup_free(&loaded);
    error_list_free(&errors);
}

UTEST(setup, a_remove_in_a_base_applies_after_the_whole_merge_even_when_a_child_adds_the_item_again) {
    setup loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(setup_load(CHAIN, "remove-child", &loaded, &errors));
    char *apps = joined_list(document_get(loaded.merged, "apps"));
    ASSERT_STREQ("git,nano", apps);
    ASSERT_TRUE(document_get(loaded.merged, "remove") == NULL);
    free(apps);
    setup_free(&loaded);
    error_list_free(&errors);
}

UTEST(setup, a_cycle_is_named_in_full) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(load_broken("loop-a", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "setup inheritance cycle: loop-a -> loop-b -> loop-a"));
    error_list_free(&errors);
}

UTEST(setup, extends_must_name_a_setup_of_this_repo) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(load_broken("orphan", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "orphan.toml:1: extends: no setup 'nowhere' in this data repo"));
    ASSERT_FALSE(load_broken("outside", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "'../catalog/apps' is not a setup name"));
    error_list_free(&errors);
}

UTEST(setup, unknown_keys_and_bad_merges_are_reported_with_their_file) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(load_broken("typo", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "typo.toml:2: distr: unknown key"));
    ASSERT_FALSE(load_broken("mismatch", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "apps: is a string here but an array in"));
    ASSERT_FALSE(load_broken("bad-remove", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "remove.notes: unknown key"));
    error_list_free(&errors);
}

UTEST(setup, a_remove_with_the_wrong_shape_is_reported_with_its_file) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(load_broken("remove-shape", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "remove-shape.toml:4: remove.apps: must be an array"));
    ASSERT_FALSE(load_broken("remove-families", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "remove-families.toml:4: remove.extra_packages: must be a table of arrays"));
    error_list_free(&errors);
}

UTEST(setup, load_refuses_a_bad_name_and_a_missing_setup) {
    error_list errors;
    error_list_init(&errors);
    setup loaded;
    ASSERT_FALSE(setup_load(DATA, "Workstation", &loaded, &errors));
    setup_free(&loaded);
    ASSERT_TRUE(error_list_mentions(&errors, "'Workstation' is not a setup name"));
    ASSERT_FALSE(setup_load(DATA, "ghost", &loaded, &errors));
    setup_free(&loaded);
    ASSERT_TRUE(error_list_mentions(&errors, "no setup 'ghost' in"));
    error_list_free(&errors);
}
