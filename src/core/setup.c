#include "setup.h"

#include "identifier.h"
#include "platform.h"
#include "setup_merge.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

static const char *const SETUP_KEYS[] = {
    "extends", "remove", "distro", "window_managers", "apps", "notes", "bootloader", "display_manager", "disk",
    "install", "user_services", "host_files", "persistence_size_mb", "extra_packages", "extra_aur",
    "extra_services", "flatpaks", "aur_helper", "cursor_extensions", "app_config", "users", "root", "system",
    "console", "remote",
};

static char *setups_directory(const char *data_directory) {
    return path_join(data_directory, "setups");
}

static char *setup_path(const char *data_directory, const char *name) {
    char *directory = setups_directory(data_directory);
    char *file_name = text_format("%s.toml", name);
    char *path = path_join(directory, file_name);
    free(directory);
    free(file_name);
    return path;
}

bool setup_list(const char *data_directory, string_list *names, error_list *errors) {
    char *directory = setups_directory(data_directory);
    if (!platform_is_directory(directory)) {
        error_push(errors, directory, 0, "missing: the data submodule is not checked out (run: git submodule update --init)");
        free(directory);
        return false;
    }
    string_list files;
    string_list_init(&files);
    bool valid = platform_list_files(directory, &files, errors);
    string_list_sort(&files);
    for (size_t index = 0; index < files.count; index++) {
        if (!text_ends_with(files.items[index], ".toml")) {
            continue;
        }
        char *name = text_without_suffix(files.items[index], ".toml");
        if (setup_name_is_valid(name)) {
            string_list_add(names, name);
        } else {
            char *path = path_join(directory, files.items[index]);
            error_push(errors, path, 0, "'%s' is not a setup name: %s", name, SETUP_NAME_RULE);
            free(path);
            valid = false;
        }
        free(name);
    }
    string_list_sort(names);
    string_list_free(&files);
    free(directory);
    return valid;
}

static bool is_list_of_names(const document_node *node, error_list *errors) {
    if (node->kind != DOCUMENT_ARRAY) {
        document_error(errors, node, "must be an array");
        return false;
    }
    return true;
}

static bool check_package_families(const document_node *packages, error_list *errors) {
    if (packages->kind != DOCUMENT_TABLE) {
        document_error(errors, packages, "must be a table of arrays, one per package family");
        return false;
    }
    bool valid = true;
    for (size_t index = 0; index < packages->child_count; index++) {
        valid = is_list_of_names(packages->children[index], errors) && valid;
    }
    return valid;
}

static bool check_remove_shape(const document_node *remove, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < remove->child_count; index++) {
        const document_node *entry = remove->children[index];
        bool is_packages = strcmp(entry->key, "extra_packages") == 0;
        valid = (is_packages ? check_package_families(entry, errors) : is_list_of_names(entry, errors)) && valid;
    }
    return valid;
}

static bool check_remove(const document_node *own, error_list *errors) {
    const document_node *remove = NULL;
    if (!document_optional_table(own, "remove", &remove, errors)) {
        return false;
    }
    if (remove == NULL) {
        return true;
    }
    bool valid = document_check_keys(remove, SETUP_REMOVE_KEYS, SETUP_REMOVE_KEY_COUNT, errors);
    return check_remove_shape(remove, errors) && valid;
}

static bool check_own_keys(const document_node *own, error_list *errors) {
    bool valid = document_check_keys(own, SETUP_KEYS, COUNT_OF(SETUP_KEYS), errors);
    return check_remove(own, errors) && valid;
}

static bool read_extends(const document_node *own, const char **extends, error_list *errors) {
    if (!document_optional_string(own, "extends", NULL, extends, errors)) {
        return false;
    }
    if (*extends != NULL && !setup_name_is_valid(*extends)) {
        document_error(errors, document_get(own, "extends"), "'%s' is not a setup name: %s", *extends, SETUP_NAME_RULE);
        return false;
    }
    return true;
}

static bool refuse_cycle(const char *name, const document_node *extends_node, string_list *chain, error_list *errors) {
    if (!string_list_contains(chain, name)) {
        return true;
    }
    string_list cycle;
    string_list_init(&cycle);
    for (size_t index = 0; index < chain->count; index++) {
        string_list_add(&cycle, chain->items[index]);
    }
    string_list_add(&cycle, name);
    char *joined = string_list_join(&cycle, " -> ");
    document_error(errors, extends_node, "setup inheritance cycle: %s", joined);
    free(joined);
    string_list_free(&cycle);
    return false;
}

static document_node *read_own(const char *data_directory, const char *name, const document_node *extends_node,
                               error_list *errors) {
    char *path = setup_path(data_directory, name);
    if (!platform_is_file(path)) {
        if (extends_node == NULL) {
            char *directory = setups_directory(data_directory);
            error_push(errors, NULL, 0, "no setup '%s' in %s", name, directory);
            free(directory);
        } else {
            document_error(errors, extends_node, "no setup '%s' in this data repo", name);
        }
        free(path);
        return NULL;
    }
    document_node *own = document_load_file(path, errors);
    free(path);
    return own;
}

static document_node *load_chain(const char *data_directory, const char *name, const document_node *extends_node,
                                 string_list *chain, document_node *removes, error_list *errors) {
    if (extends_node != NULL && !refuse_cycle(name, extends_node, chain, errors)) {
        return NULL;
    }
    document_node *own = read_own(data_directory, name, extends_node, errors);
    if (own == NULL) {
        return NULL;
    }
    string_list_add(chain, name);
    const char *extends = NULL;
    bool valid = check_own_keys(own, errors);
    valid = read_extends(own, &extends, errors) && valid;
    document_node *merged = NULL;
    if (valid && extends == NULL) {
        merged = document_clone(own);
    } else if (valid) {
        document_node *base = load_chain(data_directory, extends, document_get(own, "extends"), chain, removes, errors);
        merged = base == NULL ? NULL : setup_merge(base, own, errors);
        document_free(base);
    }
    const document_node *remove = document_get(own, "remove");
    if (merged != NULL && remove != NULL) {
        document_array_append(removes, document_clone(remove));
    }
    document_free(own);
    return merged;
}

bool setup_load(const char *data_directory, const char *name, setup *loaded, error_list *errors) {
    memset(loaded, 0, sizeof *loaded);
    string_list_init(&loaded->chain);
    if (!setup_name_is_valid(name)) {
        error_push(errors, NULL, 0, "'%s' is not a setup name: %s", name, SETUP_NAME_RULE);
        return false;
    }
    document_node *removes = document_new_array(NULL, 0);
    document_node *merged = load_chain(data_directory, name, NULL, &loaded->chain, removes, errors);
    if (merged == NULL) {
        document_free(removes);
        return false;
    }
    setup_apply_removes(merged, removes);
    document_free(removes);
    document_table_remove(merged, "extends");
    document_table_remove(merged, "remove");
    string_list_reverse(&loaded->chain);
    loaded->name = text_duplicate(name);
    loaded->file = setup_path(data_directory, name);
    loaded->merged = merged;
    return true;
}

void setup_free(setup *loaded) {
    free(loaded->name);
    free(loaded->file);
    document_free(loaded->merged);
    string_list_free(&loaded->chain);
    memset(loaded, 0, sizeof *loaded);
}
