#include "catalog.h"
#include "text.h"
#include "utest.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIRECTORY(path) _mkdir(path)
#define REMOVE_DIRECTORY(path) _rmdir(path)
#else
#include <unistd.h>
#define MAKE_DIRECTORY(path) mkdir(path, 0755)
#define REMOVE_DIRECTORY(path) rmdir(path)
#endif

#define CATALOG IMPRINT_TEST_FIXTURES "/catalog"
#define DATA IMPRINT_TEST_FIXTURES "/data"

UTEST(catalog, loads_every_kind_and_the_overlay) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(catalog_load(CATALOG, DATA, &loaded, &errors));
    ASSERT_EQ((size_t)0, errors.count);
    ASSERT_EQ((size_t)10, loaded.apps.count);
    ASSERT_EQ((size_t)2, loaded.distros.count);
    ASSERT_EQ((size_t)1, loaded.window_managers.count);
    ASSERT_EQ((size_t)1, loaded.bootloaders.count);
    catalog_free(&loaded);
}

UTEST(catalog, an_overlay_entry_replaces_the_catalog_entry) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(catalog_load(CATALOG, DATA, &loaded, &errors));
    const catalog_entry *firefox = catalog_find(&loaded.apps, "firefox");
    ASSERT_TRUE(firefox->overridden);
    ASSERT_TRUE(text_ends_with(firefox->origin, "firefox.toml"));
    const document_node *packages = document_get(catalog_app_recipe(firefox, "arch"), "packages");
    ASSERT_STREQ("firefox-i18n-de", packages->children[1]->string_value);
    ASSERT_TRUE(catalog_app_recipe(firefox, "debian") == NULL);
    ASSERT_FALSE(catalog_find(&loaded.apps, "local-tool")->overridden);
    catalog_free(&loaded);
}

UTEST(catalog, without_data_only_the_catalog_loads) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(catalog_load(CATALOG, NULL, &loaded, &errors));
    ASSERT_EQ((size_t)9, loaded.apps.count);
    const catalog_entry *firefox = catalog_find(&loaded.apps, "firefox");
    ASSERT_FALSE(firefox->overridden);
    ASSERT_TRUE(text_ends_with(firefox->origin, "firefox.json"));
    ASSERT_TRUE(catalog_find(&loaded.apps, "local-tool") == NULL);
    catalog_free(&loaded);
}

UTEST(catalog, recipe_lookup_by_distro) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(catalog_load(CATALOG, NULL, &loaded, &errors));
    const catalog_entry *synaptic = catalog_find(&loaded.apps, "synaptic");
    ASSERT_TRUE(catalog_app_recipe(catalog_find(&loaded.apps, "steam"), "arch") != NULL);
    ASSERT_TRUE(catalog_app_recipe(synaptic, "arch") == NULL);
    ASSERT_TRUE(catalog_app_recipe(synaptic, "debian") != NULL);
    ASSERT_STREQ("Synaptic", synaptic->name);
    catalog_free(&loaded);
}

UTEST(catalog, every_broken_entry_is_reported) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(catalog_load(IMPRINT_TEST_FIXTURES "/broken/catalog", NULL, &loaded, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "the file is named 'wrong-name' but its id is 'other-name'"));
    ASSERT_TRUE(error_list_mentions(&errors, "no-install.json: missing required key 'install'"));
    ASSERT_TRUE(error_list_mentions(&errors, "install.arch.pakages: unknown key"));
    ASSERT_TRUE(error_list_mentions(&errors, "'Upper' is not a valid id"));
    ASSERT_TRUE(error_list_mentions(&errors, "install.gentoo: no distro 'gentoo' in the catalog"));
    ASSERT_TRUE(error_list_mentions(&errors, "required_apps[0]: unknown app 'sway'"));
    ASSERT_TRUE(catalog_find(&loaded.apps, "git") != NULL);
    catalog_free(&loaded);
    error_list_free(&errors);
}

UTEST(catalog, a_partial_overlay_says_it_replaces_the_whole_entry) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(catalog_load(CATALOG, IMPRINT_TEST_FIXTURES "/broken/data", &loaded, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "replaces the catalog's 'git' as a whole"));
    catalog_free(&loaded);
    error_list_free(&errors);
}

UTEST(catalog, a_catalog_that_is_not_checked_out_says_how_to_fix_it) {
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(catalog_load(IMPRINT_TEST_FIXTURES "/empty-root/catalog", NULL, &loaded, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "git submodule update --init"));
    ASSERT_FALSE(error_list_mentions(&errors, "no entries"));
    catalog_free(&loaded);
    error_list_free(&errors);
}

UTEST(catalog, a_really_empty_catalog_folder_says_how_to_fix_it) {
    const char *empty_directory = "imprint-test-empty-catalog";
    REMOVE_DIRECTORY(empty_directory);
    ASSERT_EQ(0, MAKE_DIRECTORY(empty_directory));
    catalog loaded;
    error_list errors;
    error_list_init(&errors);
    bool loaded_ok = catalog_load(empty_directory, empty_directory, &loaded, &errors);
    bool says_how_to_fix = error_list_mentions(&errors, "git submodule update --init");
    catalog_free(&loaded);
    error_list_free(&errors);
    REMOVE_DIRECTORY(empty_directory);
    ASSERT_FALSE(loaded_ok);
    ASSERT_TRUE(says_how_to_fix);
}
