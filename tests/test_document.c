#include "document.h"
#include "utest.h"

#include <stdlib.h>

static document_node *sample(void) {
    document_node *root = document_new_table("base.toml", 0);
    document_table_set(root, "distro", document_new_string("arch", "base.toml", 1));
    document_node *apps = document_new_array("base.toml", 2);
    document_array_append(apps, document_new_string("git", "base.toml", 2));
    document_array_append(apps, document_new_integer(7, "base.toml", 2));
    document_table_set(root, "apps", apps);
    document_node *disk = document_new_table("base.toml", 4);
    document_table_set(disk, "esp_size_mb", document_new_integer(1024, "base.toml", 5));
    document_table_set(root, "disk", disk);
    document_node *host_files = document_new_array("base.toml", 7);
    document_node *entry = document_new_table("base.toml", 7);
    document_table_set(entry, "source", document_new_string("tools/drive-sync", "base.toml", 8));
    document_array_append(host_files, entry);
    document_table_set(root, "host_files", host_files);
    return root;
}

UTEST(document, get_finds_values_and_returns_null_otherwise) {
    document_node *root = sample();
    ASSERT_STREQ("arch", document_get(root, "distro")->string_value);
    ASSERT_TRUE(document_get(root, "missing") == NULL);
    ASSERT_TRUE(document_get(document_get(root, "distro"), "anything") == NULL);
    ASSERT_TRUE(document_get(NULL, "distro") == NULL);
    document_free(root);
}

UTEST(document, table_set_replaces_an_existing_key_in_place) {
    document_node *root = sample();
    document_table_set(root, "distro", document_new_string("debian", "child.toml", 1));
    ASSERT_EQ((size_t)4, root->child_count);
    ASSERT_STREQ("distro", root->children[0]->key);
    ASSERT_STREQ("debian", document_get(root, "distro")->string_value);
    document_free(root);
}

UTEST(document, require_string_reports_a_missing_key) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    const char *name = NULL;
    ASSERT_FALSE(document_require_string(root, "name", &name, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "base.toml: missing required key 'name'"));
    error_list_free(&errors);
    document_free(root);
}

UTEST(document, require_string_from_a_null_table_names_the_key) {
    error_list errors;
    error_list_init(&errors);
    const char *name = NULL;
    ASSERT_FALSE(document_require_string(NULL, "name", &name, &errors));
    ASSERT_EQ((size_t)1, errors.count);
    ASSERT_TRUE(error_list_mentions(&errors, "missing required key 'name'"));
    error_list_free(&errors);
}

UTEST(document, require_string_reports_the_wrong_kind_with_its_line) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    const char *value = NULL;
    ASSERT_FALSE(document_require_string(root, "disk", &value, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "base.toml:4: disk: must be a string, found table"));
    error_list_free(&errors);
    document_free(root);
}

UTEST(document, string_list_reports_the_item_that_is_not_a_string) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    string_list apps;
    string_list_init(&apps);
    ASSERT_FALSE(document_string_list(root, "apps", &apps, &errors));
    ASSERT_EQ((size_t)1, apps.count);
    ASSERT_STREQ("git", apps.items[0]);
    ASSERT_TRUE(error_list_mentions(&errors, "base.toml:2: apps[1]: must be a string, found integer"));
    string_list_free(&apps);
    error_list_free(&errors);
    document_free(root);
}

UTEST(document, string_list_of_an_absent_key_adds_nothing) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    string_list values;
    string_list_init(&values);
    ASSERT_TRUE(document_string_list(root, "flatpaks", &values, &errors));
    ASSERT_EQ((size_t)0, values.count);
    ASSERT_EQ((size_t)0, errors.count);
    document_free(root);
}

UTEST(document, path_names_nested_array_items) {
    document_node *root = sample();
    const document_node *entry = document_get(root, "host_files")->children[0];
    char *path = document_path(document_get(entry, "source"));
    char *root_path = document_path(root);
    ASSERT_STREQ("host_files[0].source", path);
    ASSERT_STREQ("", root_path);
    free(path);
    free(root_path);
    document_free(root);
}

UTEST(document, clone_is_equal_and_independent) {
    document_node *root = sample();
    document_node *copy = document_clone(root);
    ASSERT_TRUE(document_equal(root, copy));
    document_table_set(copy, "distro", document_new_string("fedora", NULL, 0));
    ASSERT_FALSE(document_equal(root, copy));
    ASSERT_STREQ("arch", document_get(root, "distro")->string_value);
    ASSERT_STREQ("base.toml", document_get(copy, "apps")->file);
    document_free(copy);
    document_free(root);
}

UTEST(document, equal_ignores_key_order_but_not_array_order) {
    document_node *first = document_new_table(NULL, 0);
    document_table_set(first, "a", document_new_integer(1, NULL, 0));
    document_table_set(first, "b", document_new_integer(2, NULL, 0));
    document_node *second = document_new_table(NULL, 0);
    document_table_set(second, "b", document_new_integer(2, NULL, 0));
    document_table_set(second, "a", document_new_integer(1, NULL, 0));
    ASSERT_TRUE(document_equal(first, second));

    document_node *forward = document_new_array(NULL, 0);
    document_array_append(forward, document_new_string("x", NULL, 0));
    document_array_append(forward, document_new_string("y", NULL, 0));
    document_node *backward = document_new_array(NULL, 0);
    document_array_append(backward, document_new_string("y", NULL, 0));
    document_array_append(backward, document_new_string("x", NULL, 0));
    ASSERT_FALSE(document_equal(forward, backward));
    document_free(first);
    document_free(second);
    document_free(forward);
    document_free(backward);
}

UTEST(document, check_keys_reports_each_unknown_key) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    static const char *const allowed[] = { "distro", "apps", "disk" };
    ASSERT_FALSE(document_check_keys(root, allowed, 3, &errors));
    ASSERT_EQ((size_t)1, errors.count);
    ASSERT_TRUE(error_list_mentions(&errors, "base.toml:7: host_files: unknown key"));
    error_list_free(&errors);
    document_free(root);
}

UTEST(document, optional_getters_use_the_fallback_and_check_the_kind) {
    document_node *root = sample();
    error_list errors;
    error_list_init(&errors);
    const char *bootloader = NULL;
    long long esp_size = 0;
    bool flag = true;
    const document_node *missing = root;
    ASSERT_TRUE(document_optional_string(root, "bootloader", "grub", &bootloader, &errors));
    ASSERT_STREQ("grub", bootloader);
    ASSERT_TRUE(document_optional_integer(document_get(root, "disk"), "esp_size_mb", 0, &esp_size, &errors));
    ASSERT_EQ(1024LL, esp_size);
    ASSERT_TRUE(document_optional_table(root, "install", &missing, &errors));
    ASSERT_TRUE(missing == NULL);
    ASSERT_FALSE(document_optional_boolean(root, "distro", false, &flag, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "distro: must be a boolean, found string"));
    error_list_free(&errors);
    document_free(root);
}

UTEST(document, remove_by_key_and_by_index) {
    document_node *root = sample();
    ASSERT_TRUE(document_table_remove(root, "disk"));
    ASSERT_FALSE(document_table_remove(root, "disk"));
    document_node *apps = document_get_mutable(root, "apps");
    document_array_remove_at(apps, 0);
    ASSERT_EQ((size_t)1, apps->child_count);
    ASSERT_TRUE(apps->children[0]->kind == DOCUMENT_INTEGER);
    document_free(root);
}

UTEST(document, skip_byte_order_mark) {
    ASSERT_STREQ("distro", document_skip_byte_order_mark("\xEF\xBB\xBF" "distro"));
    ASSERT_STREQ("distro", document_skip_byte_order_mark("distro"));
}
