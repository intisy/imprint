#include "setup_merge.h"
#include "utest.h"

#include <stdlib.h>

static document_node *toml(const char *text, const char *file) {
    error_list errors;
    error_list_init(&errors);
    document_node *root = document_parse_toml(text, file, &errors);
    error_list_free(&errors);
    return root;
}

static char *joined(const document_node *array) {
    string_list values;
    string_list_init(&values);
    for (size_t index = 0; index < array->child_count; index++) {
        string_list_add(&values, array->children[index]->string_value);
    }
    char *text = string_list_join(&values, ",");
    string_list_free(&values);
    return text;
}

UTEST(setup_merge, the_child_wins_on_values) {
    document_node *base = toml("distro = \"arch\"\ndisplay_manager = \"gdm\"\n", "base.toml");
    document_node *child = toml("display_manager = \"lightdm\"\n", "workstation.toml");
    error_list errors;
    error_list_init(&errors);
    document_node *merged = setup_merge(base, child, &errors);
    ASSERT_STREQ("arch", document_get(merged, "distro")->string_value);
    ASSERT_STREQ("lightdm", document_get(merged, "display_manager")->string_value);
    ASSERT_STREQ("base.toml", document_get(merged, "distro")->file);
    ASSERT_STREQ("workstation.toml", document_get(merged, "display_manager")->file);
    document_free(base);
    document_free(child);
    document_free(merged);
}

UTEST(setup_merge, tables_merge_key_by_key) {
    document_node *base = toml("[install]\ntimezone = \"UTC\"\nlocale = \"en_US.UTF-8\"\n", "base.toml");
    document_node *child = toml("[install]\ntimezone = \"Europe/Berlin\"\nusername = \"finn\"\n", "workstation.toml");
    error_list errors;
    error_list_init(&errors);
    document_node *merged = setup_merge(base, child, &errors);
    const document_node *install = document_get(merged, "install");
    ASSERT_EQ((size_t)3, install->child_count);
    ASSERT_STREQ("Europe/Berlin", document_get(install, "timezone")->string_value);
    ASSERT_STREQ("en_US.UTF-8", document_get(install, "locale")->string_value);
    ASSERT_STREQ("finn", document_get(install, "username")->string_value);
    document_free(base);
    document_free(child);
    document_free(merged);
}

UTEST(setup_merge, lists_are_the_union_base_first_without_duplicates) {
    document_node *base = toml("apps = [\"git\", \"base\", \"git\"]\n", "base.toml");
    document_node *child = toml("apps = [\"steam\", \"base\"]\n", "workstation.toml");
    error_list errors;
    error_list_init(&errors);
    document_node *merged = setup_merge(base, child, &errors);
    char *apps = joined(document_get(merged, "apps"));
    ASSERT_STREQ("git,base,steam", apps);
    free(apps);
    document_free(base);
    document_free(child);
    document_free(merged);
}

UTEST(setup_merge, arrays_of_tables_drop_equal_tables) {
    document_node *base = toml("[[host_files]]\nsource = \"a\"\ntarget = \"~/a\"\n", "base.toml");
    document_node *child = toml("[[host_files]]\ntarget = \"~/a\"\nsource = \"a\"\n[[host_files]]\nsource = \"b\"\ntarget = \"~/b\"\n",
                                "workstation.toml");
    error_list errors;
    error_list_init(&errors);
    document_node *merged = setup_merge(base, child, &errors);
    ASSERT_EQ((size_t)2, document_get(merged, "host_files")->child_count);
    document_free(base);
    document_free(child);
    document_free(merged);
}

UTEST(setup_merge, a_list_against_a_value_is_an_error_naming_both_files) {
    document_node *base = toml("apps = [\"git\"]\n", "base.toml");
    document_node *child = toml("apps = \"git\"\n", "workstation.toml");
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(setup_merge(base, child, &errors) == NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "workstation.toml:1: apps: is a string here but an array in base.toml"));
    error_list_free(&errors);
    document_free(base);
    document_free(child);
}

UTEST(setup_merge, removes_drop_lists_packages_and_groups) {
    document_node *merged = toml(
        "apps = [\"git\", \"synaptic\", \"steam\"]\n"
        "window_managers = [\"gnome\", \"i3\"]\n"
        "flatpaks = [\"a.b.C\"]\n"
        "[extra_packages]\narch = [\"htop\", \"rsync\"]\n"
        "[system]\nuser_groups = [\"video\", \"docker\"]\n", "merged.toml");
    document_node *removes = document_new_array(NULL, 0);
    document_array_append(removes, toml("apps = [\"synaptic\", \"not-there\"]\nwindow_managers = [\"gnome\"]\n", "base.toml"));
    document_array_append(removes, toml("user_groups = [\"docker\"]\n[extra_packages]\narch = [\"rsync\"]\n", "workstation.toml"));
    setup_apply_removes(merged, removes);
    char *apps = joined(document_get(merged, "apps"));
    char *window_managers = joined(document_get(merged, "window_managers"));
    char *packages = joined(document_get(document_get(merged, "extra_packages"), "arch"));
    char *groups = joined(document_get(document_get(merged, "system"), "user_groups"));
    ASSERT_STREQ("git,steam", apps);
    ASSERT_STREQ("i3", window_managers);
    ASSERT_STREQ("htop", packages);
    ASSERT_STREQ("video", groups);
    free(apps);
    free(window_managers);
    free(packages);
    free(groups);
    document_free(removes);
    document_free(merged);
}
