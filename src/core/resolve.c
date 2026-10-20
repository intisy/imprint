#include "resolve.h"

#include "identifier.h"
#include "text.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { RESOLVED_SETUP_FORMAT = 2 };

static const char *const DEFAULT_BOOTLOADER = "grub";

typedef struct field_offset {
    const char *key;
    size_t offset;
} field_offset;

static const field_offset TEXT_FIELDS[] = {
    { "name", offsetof(resolved_setup, name) },
    { "distro", offsetof(resolved_setup, distro) },
    { "package_manager", offsetof(resolved_setup, package_manager) },
    { "bootloader", offsetof(resolved_setup, bootloader) },
};

static const field_offset LIST_FIELDS[] = {
    { "window_managers", offsetof(resolved_setup, window_managers) },
    { "apps", offsetof(resolved_setup, apps) },
    { "packages", offsetof(resolved_setup, packages) },
    { "aur", offsetof(resolved_setup, aur) },
    { "flatpak", offsetof(resolved_setup, flatpak) },
    { "repo_setup", offsetof(resolved_setup, repo_setup) },
    { "services", offsetof(resolved_setup, services) },
    { "user_services", offsetof(resolved_setup, user_services) },
    { "skipped", offsetof(resolved_setup, skipped) },
};

static const field_offset RECIPE_FIELDS[] = {
    { "packages", offsetof(resolved_setup, packages) },
    { "aur", offsetof(resolved_setup, aur) },
    { "flatpak", offsetof(resolved_setup, flatpak) },
    { "repo_setup", offsetof(resolved_setup, repo_setup) },
    { "services", offsetof(resolved_setup, services) },
};

static const field_offset EXTRA_FIELDS[] = {
    { "extra_aur", offsetof(resolved_setup, aur) },
    { "flatpaks", offsetof(resolved_setup, flatpak) },
    { "extra_services", offsetof(resolved_setup, services) },
    { "user_services", offsetof(resolved_setup, user_services) },
};

static string_list *list_at(resolved_setup *resolved, const field_offset *field) {
    return (string_list *)((char *)resolved + field->offset);
}

static const string_list *list_at_const(const resolved_setup *resolved, const field_offset *field) {
    return (const string_list *)((const char *)resolved + field->offset);
}

static char **text_at(resolved_setup *resolved, const field_offset *field) {
    return (char **)((char *)resolved + field->offset);
}

static char *const *text_at_const(const resolved_setup *resolved, const field_offset *field) {
    return (char *const *)((const char *)resolved + field->offset);
}

void resolved_setup_init(resolved_setup *resolved) {
    memset(resolved, 0, sizeof *resolved);
    for (size_t index = 0; index < COUNT_OF(LIST_FIELDS); index++) {
        string_list_init(list_at(resolved, &LIST_FIELDS[index]));
    }
}

static void dotfiles_add(dotfile_list *dotfiles, const char *source, const char *target) {
    if (dotfiles->count == dotfiles->capacity) {
        dotfiles->capacity = dotfiles->capacity == 0 ? 4 : dotfiles->capacity * 2;
        dotfiles->items = memory_reallocate(dotfiles->items, dotfiles->capacity * sizeof *dotfiles->items);
    }
    dotfiles->items[dotfiles->count].source = text_duplicate(source);
    dotfiles->items[dotfiles->count].target = text_duplicate(target);
    dotfiles->count++;
}

static void dotfiles_free(dotfile_list *dotfiles) {
    for (size_t index = 0; index < dotfiles->count; index++) {
        free(dotfiles->items[index].source);
        free(dotfiles->items[index].target);
    }
    free(dotfiles->items);
}

void resolved_setup_free(resolved_setup *resolved) {
    for (size_t index = 0; index < COUNT_OF(TEXT_FIELDS); index++) {
        free(*text_at(resolved, &TEXT_FIELDS[index]));
    }
    for (size_t index = 0; index < COUNT_OF(LIST_FIELDS); index++) {
        string_list_free(list_at(resolved, &LIST_FIELDS[index]));
    }
    dotfiles_free(&resolved->dotfiles);
    document_free(resolved->setup);
    document_free(resolved->choices);
    resolved_setup_init(resolved);
}

static bool add_unique_from(const document_node *table, const char *key, string_list *target, error_list *errors) {
    string_list values;
    string_list_init(&values);
    bool valid = document_string_list(table, key, &values, errors);
    for (size_t index = 0; index < values.count; index++) {
        string_list_add_unique(target, values.items[index]);
    }
    string_list_free(&values);
    return valid;
}

static bool refuse_distro_word(const setup *loaded, const char *distro, error_list *errors) {
    if (!identifier_has_word(loaded->name, distro)) {
        return true;
    }
    error_push(errors, loaded->file, 0,
               "setup '%s': '%s' is a distro; a setup is named after what the system is for, and the distro is its "
               "'distro' field", loaded->name, distro);
    return false;
}

static bool check_name_against_distros(const setup *loaded, const catalog *available, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < available->distros.count; index++) {
        const catalog_entry *distro = &available->distros.items[index];
        valid = refuse_distro_word(loaded, distro->id, errors) && valid;
        const document_node *variants = document_get(distro->node, "variants");
        for (size_t variant = 0; variants != NULL && variant < variants->child_count; variant++) {
            const document_node *variant_id = document_get(variants->children[variant], "id");
            if (variant_id != NULL && strcmp(variant_id->string_value, distro->id) != 0) {
                valid = refuse_distro_word(loaded, variant_id->string_value, errors) && valid;
            }
        }
    }
    return valid;
}

static bool resolve_distro(const setup *loaded, const catalog *available, resolved_setup *resolved, error_list *errors) {
    const char *distro = NULL;
    if (!document_require_string(loaded->merged, "distro", &distro, errors)) {
        return false;
    }
    const catalog_entry *entry = catalog_find(&available->distros, distro);
    if (entry == NULL) {
        document_error(errors, document_get(loaded->merged, "distro"), "setup '%s': no distro '%s' in the catalog",
                       loaded->name, distro);
        return false;
    }
    resolved->distro = text_duplicate(distro);
    resolved->package_manager = text_duplicate(document_get(entry->node, "package_manager")->string_value);
    return true;
}

typedef bool (*id_handler)(const setup *loaded, const catalog *available, const document_node *item,
                           resolved_setup *resolved, error_list *errors);

static bool for_each_id(const setup *loaded, const catalog *available, const char *key, id_handler handle,
                        resolved_setup *resolved, error_list *errors) {
    const document_node *list = document_get(loaded->merged, key);
    if (list == NULL) {
        return true;
    }
    if (list->kind != DOCUMENT_ARRAY) {
        document_error(errors, list, "must be an array of ids, found %s", document_kind_name(list->kind));
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < list->child_count; index++) {
        const document_node *item = list->children[index];
        if (item->kind != DOCUMENT_STRING) {
            document_error(errors, item, "must be a string, found %s", document_kind_name(item->kind));
            valid = false;
            continue;
        }
        valid = handle(loaded, available, item, resolved, errors) && valid;
    }
    return valid;
}

static bool take_app(const setup *loaded, const catalog *available, const document_node *item,
                     resolved_setup *resolved, error_list *errors) {
    if (catalog_find(&available->apps, item->string_value) == NULL) {
        document_error(errors, item, "setup '%s': unknown app '%s'", loaded->name, item->string_value);
        return false;
    }
    string_list_add_unique(&resolved->apps, item->string_value);
    return true;
}

/** @implNote catalog_validate has already checked that config_files is a table of strings. */
static bool add_window_manager_dotfiles(const document_node *window_manager, dotfile_list *dotfiles) {
    const document_node *directory = document_get(window_manager, "dotfiles_directory");
    const document_node *files = document_get(window_manager, "config_files");
    for (size_t index = 0; directory != NULL && files != NULL && index < files->child_count; index++) {
        const document_node *file = files->children[index];
        char *source = text_format("dotfiles/%s/%s", directory->string_value, file->string_value);
        dotfiles_add(dotfiles, source, file->key);
        free(source);
    }
    return true;
}

static bool take_window_manager(const setup *loaded, const catalog *available, const document_node *item,
                                resolved_setup *resolved, error_list *errors) {
    const catalog_entry *entry = catalog_find(&available->window_managers, item->string_value);
    if (entry == NULL) {
        document_error(errors, item, "setup '%s': unknown window manager '%s'", loaded->name, item->string_value);
        return false;
    }
    string_list_add_unique(&resolved->window_managers, item->string_value);
    return add_unique_from(entry->node, "required_apps", &resolved->apps, errors) &&
           add_window_manager_dotfiles(entry->node, &resolved->dotfiles);
}

static bool resolve_bootloader(const setup *loaded, const catalog *available, resolved_setup *resolved,
                               error_list *errors) {
    const char *bootloader = NULL;
    if (!document_optional_string(loaded->merged, "bootloader", DEFAULT_BOOTLOADER, &bootloader, errors)) {
        return false;
    }
    if (catalog_find(&available->bootloaders, bootloader) == NULL) {
        error_push(errors, loaded->file, 0, "setup '%s': no bootloader '%s' in the catalog", loaded->name, bootloader);
        return false;
    }
    resolved->bootloader = text_duplicate(bootloader);
    return true;
}

static bool collect_recipes(const catalog *available, resolved_setup *resolved, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < resolved->apps.count; index++) {
        const catalog_entry *app = catalog_find(&available->apps, resolved->apps.items[index]);
        const document_node *recipe = catalog_app_recipe(app, resolved->distro);
        if (recipe == NULL) {
            string_list_add_unique(&resolved->skipped, app->id);
            continue;
        }
        for (size_t field = 0; field < COUNT_OF(RECIPE_FIELDS); field++) {
            valid = add_unique_from(recipe, RECIPE_FIELDS[field].key, list_at(resolved, &RECIPE_FIELDS[field]), errors) &&
                    valid;
        }
        valid = add_unique_from(app->node, "services", &resolved->services, errors) && valid;
    }
    return valid;
}

static bool collect_extras(const setup *loaded, resolved_setup *resolved, error_list *errors) {
    const document_node *extra_packages = NULL;
    bool valid = document_optional_table(loaded->merged, "extra_packages", &extra_packages, errors);
    if (extra_packages != NULL) {
        valid = add_unique_from(extra_packages, resolved->distro, &resolved->packages, errors) && valid;
    }
    for (size_t field = 0; field < COUNT_OF(EXTRA_FIELDS); field++) {
        valid = add_unique_from(loaded->merged, EXTRA_FIELDS[field].key, list_at(resolved, &EXTRA_FIELDS[field]), errors) &&
                valid;
    }
    return valid;
}

bool setup_resolve(const setup *loaded, const catalog *available, resolved_setup *resolved, error_list *errors) {
    resolved_setup_init(resolved);
    resolved->name = text_duplicate(loaded->name);
    bool valid = check_name_against_distros(loaded, available, errors);
    valid = resolve_distro(loaded, available, resolved, errors) && valid;
    valid = for_each_id(loaded, available, "apps", take_app, resolved, errors) && valid;
    valid = for_each_id(loaded, available, "window_managers", take_window_manager, resolved, errors) && valid;
    valid = resolve_bootloader(loaded, available, resolved, errors) && valid;
    if (!valid) {
        return false;
    }
    valid = collect_recipes(available, resolved, errors);
    valid = collect_extras(loaded, resolved, errors) && valid;
    resolved->setup = document_clone(loaded->merged);
    return valid;
}

char *resolved_setup_to_json(const resolved_setup *resolved) {
    document_node *root = document_new_table(NULL, 0);
    document_table_set(root, "format", document_new_integer(RESOLVED_SETUP_FORMAT, NULL, 0));
    for (size_t index = 0; index < COUNT_OF(TEXT_FIELDS); index++) {
        char *const value = *text_at_const(resolved, &TEXT_FIELDS[index]);
        document_table_set(root, TEXT_FIELDS[index].key, document_new_string(value == NULL ? "" : value, NULL, 0));
    }
    for (size_t index = 0; index < COUNT_OF(LIST_FIELDS); index++) {
        const string_list *values = list_at_const(resolved, &LIST_FIELDS[index]);
        document_node *array = document_new_array(NULL, 0);
        for (size_t item = 0; item < values->count; item++) {
            document_array_append(array, document_new_string(values->items[item], NULL, 0));
        }
        document_table_set(root, LIST_FIELDS[index].key, array);
    }
    document_node *dotfiles = document_new_array(NULL, 0);
    for (size_t index = 0; index < resolved->dotfiles.count; index++) {
        document_node *pair = document_new_table(NULL, 0);
        document_table_set(pair, "source", document_new_string(resolved->dotfiles.items[index].source, NULL, 0));
        document_table_set(pair, "target", document_new_string(resolved->dotfiles.items[index].target, NULL, 0));
        document_array_append(dotfiles, pair);
    }
    document_table_set(root, "dotfiles", dotfiles);
    document_table_set(root, "setup", resolved->setup == NULL ? document_new_table(NULL, 0) : document_clone(resolved->setup));
    if (resolved->choices != NULL) {
        document_table_set(root, "choices", document_clone(resolved->choices));
    }
    char *json = document_to_json(root);
    document_free(root);
    return json;
}

static bool read_format(const document_node *root, error_list *errors) {
    const document_node *found = document_get(root, "format");
    if (found == NULL) {
        document_error(errors, root, "missing required key 'format'");
        return false;
    }
    long long format = 0;
    if (!document_optional_integer(root, "format", 0, &format, errors)) {
        return false;
    }
    if (format != RESOLVED_SETUP_FORMAT) {
        document_error(errors, root, "format %lld is not %d: the manager and the engine come from different imprint versions",
                       format, RESOLVED_SETUP_FORMAT);
        return false;
    }
    return true;
}

static bool is_resolved_key(const char *key) {
    if (strcmp(key, "format") == 0 || strcmp(key, "setup") == 0 || strcmp(key, "dotfiles") == 0 ||
        strcmp(key, "choices") == 0) {
        return true;
    }
    for (size_t index = 0; index < COUNT_OF(TEXT_FIELDS); index++) {
        if (strcmp(key, TEXT_FIELDS[index].key) == 0) {
            return true;
        }
    }
    for (size_t index = 0; index < COUNT_OF(LIST_FIELDS); index++) {
        if (strcmp(key, LIST_FIELDS[index].key) == 0) {
            return true;
        }
    }
    return false;
}

static bool refuse_unknown_keys(const document_node *root, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < root->child_count; index++) {
        if (!is_resolved_key(root->children[index]->key)) {
            document_error(errors, root->children[index], "unknown key '%s' in a resolved setup", root->children[index]->key);
            valid = false;
        }
    }
    return valid;
}

static bool require_list(const document_node *root, const field_offset *field, string_list *values, error_list *errors) {
    if (document_get(root, field->key) == NULL) {
        document_error(errors, root, "missing required key '%s'", field->key);
        return false;
    }
    return document_string_list(root, field->key, values, errors);
}

static bool read_dotfiles(const document_node *root, dotfile_list *dotfiles, error_list *errors) {
    const document_node *list = document_get(root, "dotfiles");
    if (list == NULL || list->kind != DOCUMENT_ARRAY) {
        document_error(errors, list == NULL ? root : list, "missing required key 'dotfiles', an array");
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < list->child_count; index++) {
        const char *source = NULL;
        const char *target = NULL;
        const document_node *pair = list->children[index];
        if (pair->kind != DOCUMENT_TABLE) {
            document_error(errors, pair, "a dotfile is an object with 'source' and 'target'");
            valid = false;
            continue;
        }
        bool complete = document_require_string(pair, "source", &source, errors);
        complete = document_require_string(pair, "target", &target, errors) && complete;
        if (complete) {
            dotfiles_add(dotfiles, source, target);
        }
        valid = complete && valid;
    }
    return valid;
}

bool resolved_setup_from_json(const char *text, const char *file, resolved_setup *resolved, error_list *errors) {
    resolved_setup_init(resolved);
    document_node *root = document_parse_json(text, file, errors);
    if (root == NULL) {
        return false;
    }
    if (root->kind != DOCUMENT_TABLE) {
        document_error(errors, root, "a resolved setup is a JSON object");
        document_free(root);
        return false;
    }
    bool valid = read_format(root, errors) && refuse_unknown_keys(root, errors);
    for (size_t index = 0; valid && index < COUNT_OF(TEXT_FIELDS); index++) {
        const char *value = NULL;
        if (document_require_string(root, TEXT_FIELDS[index].key, &value, errors)) {
            *text_at(resolved, &TEXT_FIELDS[index]) = text_duplicate(value);
        } else {
            valid = false;
        }
    }
    for (size_t index = 0; valid && index < COUNT_OF(LIST_FIELDS); index++) {
        valid = require_list(root, &LIST_FIELDS[index], list_at(resolved, &LIST_FIELDS[index]), errors);
    }
    valid = valid && read_dotfiles(root, &resolved->dotfiles, errors);
    const document_node *setup_table = NULL;
    if (valid && document_optional_table(root, "setup", &setup_table, errors) && setup_table != NULL) {
        resolved->setup = document_clone(setup_table);
    } else if (valid) {
        document_error(errors, root, "missing required key 'setup'");
        valid = false;
    }
    const document_node *choices = NULL;
    if (valid && document_optional_table(root, "choices", &choices, errors)) {
        resolved->choices = choices == NULL ? NULL : document_clone(choices);
    } else {
        valid = false;
    }
    document_free(root);
    return valid;
}
