#include "resolve.h"
#include "text.h"
#include "utest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATALOG IMPRINT_TEST_FIXTURES "/catalog"
#define DATA IMPRINT_TEST_FIXTURES "/data"
#define BROKEN IMPRINT_TEST_FIXTURES "/broken/data"

static bool resolve_named(const char *data, const char *overlay, const char *name, resolved_setup *resolved,
                          error_list *errors) {
    catalog available;
    setup loaded;
    memset(&loaded, 0, sizeof loaded);
    resolved_setup_init(resolved);
    bool valid = catalog_load(CATALOG, overlay, &available, errors) && setup_load(data, name, &loaded, errors) &&
                 setup_resolve(&loaded, &available, resolved, errors);
    setup_free(&loaded);
    catalog_free(&available);
    return valid;
}

static void expect_list(int *utest_result, const string_list *list, const char *expected) {
    char *joined = string_list_join(list, ",");
    ASSERT_STREQ(expected, joined);
    free(joined);
}

UTEST(resolve, workstation_collects_every_recipe_and_extra) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolve_named(DATA, DATA, "workstation", &resolved, &errors));
    ASSERT_EQ((size_t)0, errors.count);
    ASSERT_STREQ("arch", resolved.distro);
    ASSERT_STREQ("pacman", resolved.package_manager);
    ASSERT_STREQ("grub", resolved.bootloader);
    expect_list(utest_result, &resolved.window_managers, "i3");
    expect_list(utest_result, &resolved.apps,
                "git,firefox,steam,mangohud,visual-studio-code,obsidian,tailscale,local-tool,i3");
    expect_list(utest_result, &resolved.packages,
                "git,base-devel,firefox,firefox-i18n-de,steam,mangohud,lib32-mangohud,tailscale,local-tool,"
                "i3-wm,i3status,htop");
    expect_list(utest_result, &resolved.aur, "visual-studio-code-bin,yay-bin");
    expect_list(utest_result, &resolved.flatpak, "md.obsidian.Obsidian,com.example.App");
    expect_list(utest_result, &resolved.services, "tailscaled.service,fstrim.timer");
    expect_list(utest_result, &resolved.user_services, "syncthing.service");
    ASSERT_EQ((size_t)1, resolved.repo_setup.count);
    ASSERT_EQ((size_t)0, resolved.skipped.count);
    resolved_setup_free(&resolved);
}

UTEST(resolve, an_app_without_a_recipe_for_the_distro_is_skipped) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolve_named(DATA, DATA, "base", &resolved, &errors));
    expect_list(utest_result, &resolved.skipped, "synaptic");
    expect_list(utest_result, &resolved.packages, "git,base-devel,firefox,firefox-i18n-de,i3-wm,i3status,htop,rsync");
    resolved_setup_free(&resolved);
}

UTEST(resolve, unknown_ids_name_the_setup_and_the_id) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(resolve_named(BROKEN, NULL, "unknown-app", &resolved, &errors));
    resolved_setup_free(&resolved);
    ASSERT_TRUE(error_list_mentions(&errors, "unknown-app.toml:2: apps[1]: setup 'unknown-app': unknown app 'stean'"));
    ASSERT_FALSE(resolve_named(BROKEN, NULL, "missing-desktop", &resolved, &errors));
    resolved_setup_free(&resolved);
    ASSERT_TRUE(error_list_mentions(&errors, "setup 'missing-desktop': unknown window manager 'i4'"));
    error_list_free(&errors);
}

UTEST(resolve, a_name_that_names_a_distro_is_refused) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(resolve_named(BROKEN, NULL, "workstation-arch", &resolved, &errors));
    resolved_setup_free(&resolved);
    ASSERT_TRUE(error_list_mentions(&errors, "setup 'workstation-arch': 'arch' is a distro"));
    error_list_free(&errors);
}

UTEST(resolve, a_setup_needs_a_distro) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(resolve_named(BROKEN, NULL, "no-distro", &resolved, &errors));
    resolved_setup_free(&resolved);
    ASSERT_TRUE(error_list_mentions(&errors, "no-distro.toml"));
    ASSERT_TRUE(error_list_mentions(&errors, "missing required key 'distro'"));
    error_list_free(&errors);
}

UTEST(resolve, json_round_trips) {
    resolved_setup resolved;
    resolved_setup again;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolve_named(DATA, DATA, "workstation", &resolved, &errors));
    char *json = resolved_setup_to_json(&resolved);
    ASSERT_TRUE(resolved_setup_from_json(json, "workstation.json", &again, &errors));
    ASSERT_STREQ(resolved.name, again.name);
    ASSERT_STREQ(resolved.package_manager, again.package_manager);
    char *before = string_list_join(&resolved.packages, ",");
    char *after = string_list_join(&again.packages, ",");
    ASSERT_STREQ(before, after);
    ASSERT_TRUE(document_equal(resolved.setup, again.setup));
    free(before);
    free(after);
    free(json);
    resolved_setup_free(&resolved);
    resolved_setup_free(&again);
}

UTEST(resolve, json_from_another_format_is_refused) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(resolved_setup_from_json("{\"format\": 99, \"setup\": {}}", "old.json", &resolved, &errors));
    resolved_setup_free(&resolved);
    ASSERT_TRUE(error_list_mentions(&errors, "format 99 is not 2"));
    error_list_free(&errors);
}

static bool is_refused_with(const char *json, const char *message) {
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    bool refused = !resolved_setup_from_json(json, "hand-over.json", &resolved, &errors);
    bool mentioned = error_list_mentions(&errors, message);
    resolved_setup_free(&resolved);
    error_list_free(&errors);
    return refused && mentioned;
}

static const char COMPLETE_DOCUMENT[] =
    "{\"format\": 2, \"name\": \"n\", \"distro\": \"arch\", \"package_manager\": \"pacman\", "
    "\"bootloader\": \"grub\", \"window_managers\": [], \"apps\": [], \"packages\": [], \"aur\": [], "
    "\"flatpak\": [], \"repo_setup\": [], \"services\": [], \"user_services\": [], \"skipped\": [], "
    "\"dotfiles\": [], \"setup\": {}%s}";

UTEST(resolve, a_complete_document_is_accepted) {
    char document[1024];
    snprintf(document, sizeof document, COMPLETE_DOCUMENT, "");
    resolved_setup resolved;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolved_setup_from_json(document, "hand-over.json", &resolved, &errors));
    resolved_setup_free(&resolved);
    error_list_free(&errors);
}

UTEST(resolve, a_missing_list_key_is_refused) {
    ASSERT_TRUE(is_refused_with("{\"format\": 2, \"name\": \"n\", \"distro\": \"arch\", \"package_manager\": \"pacman\", "
                   "\"bootloader\": \"grub\", \"setup\": {}}",
                   "missing required key 'window_managers'"));
}

UTEST(resolve, an_unknown_key_is_refused) {
    char document[1024];
    snprintf(document, sizeof document, COMPLETE_DOCUMENT, ", \"extra\": 1");
    ASSERT_TRUE(is_refused_with(document, "unknown key 'extra'"));
}

UTEST(resolve, install_choices_survive_json) {
    char document[1024];
    snprintf(document, sizeof document, COMPLETE_DOCUMENT,
             ", \"choices\": {\"storage\": {\"node\": \"data-disk\", \"path\": \"/data\", \"threshold\": 50}}");
    resolved_setup resolved;
    resolved_setup again;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolved_setup_from_json(document, "hand-over.json", &resolved, &errors));
    char *json = resolved_setup_to_json(&resolved);
    ASSERT_TRUE(resolved_setup_from_json(json, "again.json", &again, &errors));
    ASSERT_TRUE(again.choices != NULL);
    ASSERT_TRUE(document_equal(resolved.choices, again.choices));
    ASSERT_STREQ("/data", document_get(document_get(again.choices, "storage"), "path")->string_value);
    free(json);
    resolved_setup_free(&resolved);
    resolved_setup_free(&again);
    error_list_free(&errors);
}

UTEST(resolve, choices_that_are_not_an_object_are_refused) {
    char document[1024];
    snprintf(document, sizeof document, COMPLETE_DOCUMENT, ", \"choices\": 1");
    ASSERT_TRUE(is_refused_with(document, "choices"));
}

UTEST(resolve, a_missing_format_is_reported_as_missing) {
    ASSERT_TRUE(is_refused_with("{\"name\": \"n\", \"setup\": {}}", "missing required key 'format'"));
}

UTEST(resolve, window_manager_dotfiles_resolve_and_survive_json) {
    resolved_setup resolved;
    resolved_setup again;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(resolve_named(DATA, DATA, "workstation", &resolved, &errors));
    ASSERT_EQ((size_t)1, resolved.dotfiles.count);
    ASSERT_STREQ("dotfiles/i3/config", resolved.dotfiles.items[0].source);
    ASSERT_STREQ(".config/i3/config", resolved.dotfiles.items[0].target);
    char *json = resolved_setup_to_json(&resolved);
    ASSERT_TRUE(resolved_setup_from_json(json, "workstation.json", &again, &errors));
    ASSERT_EQ((size_t)1, again.dotfiles.count);
    ASSERT_STREQ("dotfiles/i3/config", again.dotfiles.items[0].source);
    ASSERT_STREQ(".config/i3/config", again.dotfiles.items[0].target);
    free(json);
    resolved_setup_free(&resolved);
    resolved_setup_free(&again);
    error_list_free(&errors);
}

UTEST(resolve, a_dotfile_without_a_target_is_refused) {
    char document[1024];
    snprintf(document, sizeof document, COMPLETE_DOCUMENT, "");
    char *broken = text_format("%.*s\"dotfiles\": [{\"source\": \"dotfiles/i3/config\"}]%s",
                               (int)(strstr(document, "\"dotfiles\": []") - document), document,
                               strstr(document, "\"dotfiles\": []") + strlen("\"dotfiles\": []"));
    ASSERT_TRUE(is_refused_with(broken, "target"));
    free(broken);
}
