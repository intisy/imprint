#include "catalog_validate.h"

#include "identifier.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

static const char *const APP_KEYS[] = { "id", "name", "category", "tags", "install", "services", "config", "config_files" };
static const char *const RECIPE_KEYS[] = { "packages", "aur", "flatpak", "repo_setup", "services" };
static const char *const DISTRO_KEYS[] = { "id", "name", "package_manager", "extra_sources", "persistence_label",
                                           "iso_match", "installer", "initramfs_tool", "kernel_packages", "variants" };
static const char *const VARIANT_KEYS[] = { "id", "name", "iso_match" };
static const char *const WINDOW_MANAGER_KEYS[] = { "id", "name", "type", "session", "required_apps",
                                                   "dotfiles_directory", "config_files" };
static const char *const BOOTLOADER_KEYS[] = { "id", "name", "install_command", "config_path", "mkconfig_command",
                                               "resume_command_line", "mkinitcpio_hook", "notes" };
static const char *const PACKAGE_MANAGERS[] = { "pacman", "apt", "dnf" };
static const char *const DISPLAY_TYPES[] = { "x11", "wayland" };
static const char *const SESSIONS[] = { "xinit", "display-manager" };

static bool check_string_list(const document_node *table, const char *key, error_list *errors) {
    string_list values;
    string_list_init(&values);
    bool valid = document_string_list(table, key, &values, errors);
    string_list_free(&values);
    return valid;
}

static bool check_identifier_list(const document_node *table, const char *key, error_list *errors) {
    if (!check_string_list(table, key, errors)) {
        return false;
    }
    const document_node *list = document_get(table, key);
    bool valid = true;
    for (size_t index = 0; list != NULL && index < list->child_count; index++) {
        if (!identifier_is_valid(list->children[index]->string_value)) {
            document_error(errors, list->children[index], "'%s' is not a valid id: %s",
                           list->children[index]->string_value, IDENTIFIER_RULE);
            valid = false;
        }
    }
    return valid;
}

static bool check_optional_strings(const document_node *table, const char *const *keys, size_t count, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < count; index++) {
        const char *value = NULL;
        valid = document_optional_string(table, keys[index], NULL, &value, errors) && valid;
    }
    return valid;
}

static bool check_one_of(const document_node *table, const char *key, const char *const *choices, size_t count,
                         error_list *errors) {
    const char *value = NULL;
    if (!document_require_string(table, key, &value, errors)) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if (strcmp(value, choices[index]) == 0) {
            return true;
        }
    }
    string_list allowed;
    string_list_init(&allowed);
    for (size_t index = 0; index < count; index++) {
        string_list_add(&allowed, choices[index]);
    }
    char *joined = string_list_join(&allowed, ", ");
    document_error(errors, document_get(table, key), "'%s' is not one of: %s", value, joined);
    free(joined);
    string_list_free(&allowed);
    return false;
}

static bool check_table(const document_node *table, const char *key, error_list *errors) {
    const document_node *value = NULL;
    return document_optional_table(table, key, &value, errors);
}

static bool check_recipe(const document_node *recipe, error_list *errors) {
    if (recipe->kind != DOCUMENT_TABLE) {
        document_error(errors, recipe, "must be a table, found %s", document_kind_name(recipe->kind));
        return false;
    }
    bool valid = document_check_keys(recipe, RECIPE_KEYS, COUNT_OF(RECIPE_KEYS), errors);
    for (size_t index = 0; index < COUNT_OF(RECIPE_KEYS); index++) {
        valid = check_string_list(recipe, RECIPE_KEYS[index], errors) && valid;
    }
    return valid;
}

bool catalog_validate_app(const document_node *entry, error_list *errors) {
    const char *name = NULL;
    const char *const optional[] = { "category" };
    bool valid = document_check_keys(entry, APP_KEYS, COUNT_OF(APP_KEYS), errors);
    valid = document_require_string(entry, "name", &name, errors) && valid;
    valid = check_optional_strings(entry, optional, COUNT_OF(optional), errors) && valid;
    valid = check_identifier_list(entry, "tags", errors) && valid;
    valid = check_string_list(entry, "services", errors) && valid;
    valid = check_table(entry, "config", errors) && valid;
    valid = check_table(entry, "config_files", errors) && valid;
    const document_node *install = NULL;
    if (!document_optional_table(entry, "install", &install, errors)) {
        return false;
    }
    if (install == NULL) {
        document_error(errors, entry, "missing required key 'install'");
        return false;
    }
    if (install->child_count == 0) {
        document_error(errors, install, "names no distro");
        valid = false;
    }
    for (size_t index = 0; index < install->child_count; index++) {
        valid = check_recipe(install->children[index], errors) && valid;
    }
    return valid;
}

static bool check_variants(const document_node *entry, error_list *errors) {
    const document_node *variants = document_get(entry, "variants");
    if (variants == NULL) {
        return true;
    }
    if (variants->kind != DOCUMENT_ARRAY) {
        document_error(errors, variants, "must be an array of tables, found %s", document_kind_name(variants->kind));
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < variants->child_count; index++) {
        const document_node *variant = variants->children[index];
        const char *text = NULL;
        if (variant->kind != DOCUMENT_TABLE) {
            document_error(errors, variant, "must be a table, found %s", document_kind_name(variant->kind));
            valid = false;
            continue;
        }
        valid = document_check_keys(variant, VARIANT_KEYS, COUNT_OF(VARIANT_KEYS), errors) && valid;
        valid = document_require_string(variant, "name", &text, errors) && valid;
        valid = check_string_list(variant, "iso_match", errors) && valid;
        if (document_require_string(variant, "id", &text, errors) && !identifier_is_valid(text)) {
            document_error(errors, document_get(variant, "id"), "'%s' is not a valid id: %s", text, IDENTIFIER_RULE);
            valid = false;
        }
    }
    return valid;
}

bool catalog_validate_distro(const document_node *entry, error_list *errors) {
    const char *name = NULL;
    const char *const optional[] = { "persistence_label", "installer", "initramfs_tool" };
    bool valid = document_check_keys(entry, DISTRO_KEYS, COUNT_OF(DISTRO_KEYS), errors);
    valid = document_require_string(entry, "name", &name, errors) && valid;
    valid = check_one_of(entry, "package_manager", PACKAGE_MANAGERS, COUNT_OF(PACKAGE_MANAGERS), errors) && valid;
    valid = check_optional_strings(entry, optional, COUNT_OF(optional), errors) && valid;
    valid = check_string_list(entry, "iso_match", errors) && valid;
    valid = check_string_list(entry, "kernel_packages", errors) && valid;
    valid = check_table(entry, "extra_sources", errors) && valid;
    return check_variants(entry, errors) && valid;
}

bool catalog_validate_window_manager(const document_node *entry, error_list *errors) {
    const char *text = NULL;
    bool valid = document_check_keys(entry, WINDOW_MANAGER_KEYS, COUNT_OF(WINDOW_MANAGER_KEYS), errors);
    valid = document_require_string(entry, "name", &text, errors) && valid;
    valid = check_one_of(entry, "type", DISPLAY_TYPES, COUNT_OF(DISPLAY_TYPES), errors) && valid;
    valid = check_one_of(entry, "session", SESSIONS, COUNT_OF(SESSIONS), errors) && valid;
    valid = check_identifier_list(entry, "required_apps", errors) && valid;
    valid = document_require_string(entry, "dotfiles_directory", &text, errors) && valid;
    return check_table(entry, "config_files", errors) && valid;
}

bool catalog_validate_bootloader(const document_node *entry, error_list *errors) {
    const char *name = NULL;
    const char *const optional[] = { "install_command", "config_path", "mkconfig_command",
                                     "resume_command_line", "mkinitcpio_hook", "notes" };
    bool valid = document_check_keys(entry, BOOTLOADER_KEYS, COUNT_OF(BOOTLOADER_KEYS), errors);
    valid = document_require_string(entry, "name", &name, errors) && valid;
    return check_optional_strings(entry, optional, COUNT_OF(optional), errors) && valid;
}

static bool check_required_apps(const catalog *loaded, const catalog_entry *window_manager, error_list *errors) {
    const document_node *required = document_get(window_manager->node, "required_apps");
    bool valid = true;
    for (size_t index = 0; required != NULL && index < required->child_count; index++) {
        const char *app = required->children[index]->string_value;
        if (catalog_find(&loaded->apps, app) == NULL) {
            document_error(errors, required->children[index], "unknown app '%s'", app);
            valid = false;
        }
    }
    return valid;
}

static bool check_recipe_distros(const catalog *loaded, const catalog_entry *app, error_list *errors) {
    const document_node *install = document_get(app->node, "install");
    bool valid = true;
    for (size_t index = 0; install != NULL && index < install->child_count; index++) {
        const char *distro = install->children[index]->key;
        if (catalog_find(&loaded->distros, distro) == NULL) {
            document_error(errors, install->children[index], "no distro '%s' in the catalog", distro);
            valid = false;
        }
    }
    return valid;
}

bool catalog_check_references(const catalog *loaded, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < loaded->window_managers.count; index++) {
        valid = check_required_apps(loaded, &loaded->window_managers.items[index], errors) && valid;
    }
    for (size_t index = 0; index < loaded->apps.count; index++) {
        valid = check_recipe_distros(loaded, &loaded->apps.items[index], errors) && valid;
    }
    return valid;
}
