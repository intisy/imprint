#include "catalog.h"

#include "catalog_validate.h"
#include "identifier.h"
#include "platform.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

typedef bool (*entry_validator)(const document_node *entry, error_list *errors);

typedef struct entry_kind {
    const char *label;
    const char *catalog_subdirectory;
    const char *data_subdirectory;
    entry_validator validate;
} entry_kind;

static const entry_kind ENTRY_KINDS[] = {
    { "app", "generated/apps", "catalog/apps", catalog_validate_app },
    { "distro", "curated/distros", "catalog/distros", catalog_validate_distro },
    { "window manager", "curated/window-managers", "catalog/window-managers", catalog_validate_window_manager },
    { "bootloader", "curated/bootloaders", "catalog/bootloaders", catalog_validate_bootloader },
};

static entry_set *set_of_kind(catalog *loaded, size_t kind) {
    entry_set *sets[] = { &loaded->apps, &loaded->distros, &loaded->window_managers, &loaded->bootloaders };
    return sets[kind];
}

static void entry_free(catalog_entry *entry) {
    free(entry->id);
    free(entry->name);
    free(entry->origin);
    document_free(entry->node);
}

static void entry_set_free(entry_set *set) {
    for (size_t index = 0; index < set->count; index++) {
        entry_free(&set->items[index]);
    }
    free(set->items);
    memset(set, 0, sizeof *set);
}

const catalog_entry *catalog_find(const entry_set *set, const char *id) {
    for (size_t index = 0; index < set->count; index++) {
        if (strcmp(set->items[index].id, id) == 0) {
            return &set->items[index];
        }
    }
    return NULL;
}

const document_node *catalog_app_recipe(const catalog_entry *app, const char *distro) {
    return document_get(document_get(app->node, "install"), distro);
}

static void entry_set_put(entry_set *set, catalog_entry entry) {
    catalog_entry *existing = (catalog_entry *)catalog_find(set, entry.id);
    if (existing != NULL) {
        entry_free(existing);
        *existing = entry;
        return;
    }
    if (set->count == set->capacity) {
        set->capacity = set->capacity == 0 ? 16 : set->capacity * 2;
        set->items = memory_reallocate(set->items, set->capacity * sizeof *set->items);
    }
    set->items[set->count++] = entry;
}

static bool is_entry_file(const char *name) {
    return text_ends_with(name, ".toml") || text_ends_with(name, ".json");
}

static char *entry_stem(const char *name) {
    return text_ends_with(name, ".toml") ? text_without_suffix(name, ".toml") : text_without_suffix(name, ".json");
}

static bool check_identity(const document_node *node, const char *path, const char *stem, error_list *errors) {
    const char *id = NULL;
    if (!document_require_string(node, "id", &id, errors)) {
        return false;
    }
    if (!catalog_check_identifier(document_get(node, "id"), errors)) {
        return false;
    }
    if (strcmp(id, stem) != 0) {
        error_push(errors, path, 0, "the file is named '%s' but its id is '%s': name the file after the id", stem, id);
        return false;
    }
    return true;
}

static bool load_entry(const char *directory, const char *file_name, const entry_kind *kind, const entry_set *replaced,
                       catalog_entry *entry, error_list *errors) {
    char *path = path_join(directory, file_name);
    document_node *node = document_load_file(path, errors);
    if (node == NULL) {
        free(path);
        return false;
    }
    char *stem = entry_stem(file_name);
    bool valid = check_identity(node, path, stem, errors);
    valid = kind->validate(node, errors) && valid;
    if (!valid && replaced != NULL && catalog_find(replaced, stem) != NULL) {
        error_push(errors, path, 0, "this %s replaces the catalog's '%s' as a whole, so it needs every required field",
                   kind->label, stem);
    }
    free(stem);
    if (!valid) {
        document_free(node);
        free(path);
        return false;
    }
    entry->id = text_duplicate(document_get(node, "id")->string_value);
    entry->name = text_duplicate(document_get(node, "name")->string_value);
    entry->origin = path;
    entry->overridden = replaced != NULL && catalog_find(replaced, entry->id) != NULL;
    entry->node = node;
    return true;
}

static bool load_directory(const char *directory, const entry_kind *kind, entry_set *set, bool is_overlay,
                           error_list *errors) {
    string_list names;
    string_list stems;
    string_list_init(&names);
    string_list_init(&stems);
    bool valid = platform_list_files(directory, &names, errors);
    for (size_t index = 0; index < names.count; index++) {
        if (!is_entry_file(names.items[index])) {
            continue;
        }
        char *stem = entry_stem(names.items[index]);
        bool first = string_list_add_unique(&stems, stem);
        if (!first) {
            error_push(errors, directory, 0, "the %s '%s' is defined twice, as .toml and as .json", kind->label, stem);
        }
        free(stem);
        catalog_entry entry;
        if (!first || !load_entry(directory, names.items[index], kind, is_overlay ? set : NULL, &entry, errors)) {
            valid = false;
            continue;
        }
        entry_set_put(set, entry);
    }
    string_list_free(&names);
    string_list_free(&stems);
    return valid;
}

static bool load_kind(const char *catalog_directory, const char *data_directory, size_t kind, catalog *loaded,
                      error_list *errors) {
    bool valid = true;
    char *shipped = path_join(catalog_directory, ENTRY_KINDS[kind].catalog_subdirectory);
    if (!platform_is_directory(shipped)) {
        error_push(errors, shipped, 0, "missing: the catalog submodule is not checked out (run: git submodule update --init)");
        valid = false;
    } else {
        valid = load_directory(shipped, &ENTRY_KINDS[kind], set_of_kind(loaded, kind), false, errors);
    }
    free(shipped);
    if (data_directory != NULL) {
        char *overlay = path_join(data_directory, ENTRY_KINDS[kind].data_subdirectory);
        if (platform_is_directory(overlay)) {
            valid = load_directory(overlay, &ENTRY_KINDS[kind], set_of_kind(loaded, kind), true, errors) && valid;
        }
        free(overlay);
    }
    return valid;
}

bool catalog_load(const char *catalog_directory, const char *data_directory, catalog *loaded, error_list *errors) {
    memset(loaded, 0, sizeof *loaded);
    bool valid = true;
    for (size_t kind = 0; kind < COUNT_OF(ENTRY_KINDS); kind++) {
        valid = load_kind(catalog_directory, data_directory, kind, loaded, errors) && valid;
    }
    return catalog_check_references(loaded, errors) && valid;
}

void catalog_free(catalog *loaded) {
    for (size_t kind = 0; kind < COUNT_OF(ENTRY_KINDS); kind++) {
        entry_set_free(set_of_kind(loaded, kind));
    }
}
