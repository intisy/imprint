#include "catalog.h"
#include "document.h"
#include "platform.h"
#include "resolve.h"
#include "setup.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PARITY_SKIPPED = 77 };

typedef struct parity_case {
    const char *setup_name;
    const char *reference_file;
} parity_case;

static const parity_case CASES[] = {
    { "workstation", "workstation.json" },
    { "storage-worker", "storage-worker.json" },
};

/**
 * @implNote ventoy-deploy's Resolve-Profile does not return the resolved app list, so apps are not compared.
 */
static const char *const COMPARED_LISTS[] = { "packages", "aur", "flatpak", "repo_setup",
                                              "services", "user_services", "skipped" };

static const string_list *resolved_list(const resolved_setup *resolved, const char *key) {
    const string_list *lists[] = { &resolved->packages, &resolved->aur, &resolved->flatpak,
                                   &resolved->repo_setup, &resolved->services, &resolved->user_services,
                                   &resolved->skipped };
    for (size_t index = 0; index < COUNT_OF(COMPARED_LISTS); index++) {
        if (strcmp(COMPARED_LISTS[index], key) == 0) {
            return lists[index];
        }
    }
    return NULL;
}

static size_t report_missing(const char *setup_name, const char *key, const string_list *present,
                             const string_list *other, const char *owner) {
    size_t differences = 0;
    for (size_t index = 0; index < present->count; index++) {
        if (!string_list_contains(other, present->items[index])) {
            printf("%s %s: only %s has %s\n", setup_name, key, owner, present->items[index]);
            differences++;
        }
    }
    return differences;
}

static size_t compare_list(const char *setup_name, const char *key, const resolved_setup *resolved,
                           const document_node *reference, error_list *errors) {
    string_list expected;
    string_list_init(&expected);
    document_string_list(reference, key, &expected, errors);
    const string_list *actual = resolved_list(resolved, key);
    size_t differences = report_missing(setup_name, key, actual, &expected, "imprint") +
                         report_missing(setup_name, key, &expected, actual, "ventoy-deploy");
    string_list_free(&expected);
    return differences;
}

static size_t compare_case(const char *root, const char *references, const catalog *available,
                           const parity_case *tested, error_list *errors) {
    char *data = path_join(root, "data");
    char *reference_path = path_join(references, tested->reference_file);
    setup loaded;
    memset(&loaded, 0, sizeof loaded);
    resolved_setup resolved;
    resolved_setup_init(&resolved);
    document_node *reference = document_load_file(reference_path, errors);
    size_t differences = 1;
    if (reference != NULL && setup_load(data, tested->setup_name, &loaded, errors) &&
        setup_resolve(&loaded, available, &resolved, errors)) {
        differences = 0;
        for (size_t index = 0; index < COUNT_OF(COMPARED_LISTS); index++) {
            differences += compare_list(tested->setup_name, COMPARED_LISTS[index], &resolved, reference, errors);
        }
        printf("parity: %s %s ventoy-deploy (%zu packages)\n", tested->setup_name,
               differences == 0 ? "matches" : "differs from", resolved.packages.count);
    }
    setup_free(&loaded);
    resolved_setup_free(&resolved);
    document_free(reference);
    free(reference_path);
    free(data);
    return differences;
}

static bool data_is_the_private_repo(const char *root) {
    char *probe = path_join(root, "data/setups/workstation.toml");
    bool found = platform_is_file(probe);
    free(probe);
    return found;
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3) {
        fputs("usage: imprint-parity-tests <root> <reference directory>\n", stderr);
        return 2;
    }
    const char *root = arguments[1];
    if (!data_is_the_private_repo(root)) {
        printf("skipped: data/ is not imprint-data (run: imprint data use intisy/imprint-data)\n");
        return PARITY_SKIPPED;
    }
    error_list errors;
    error_list_init(&errors);
    char *catalog_directory = path_join(root, "catalog");
    char *data = path_join(root, "data");
    catalog available;
    size_t differences = 0;
    if (catalog_load(catalog_directory, data, &available, &errors)) {
        for (size_t index = 0; index < COUNT_OF(CASES); index++) {
            differences += compare_case(root, arguments[2], &available, &CASES[index], &errors);
        }
    } else {
        differences = 1;
    }
    error_list_print(&errors, stderr);
    bool clean = differences == 0 && errors.count == 0;
    error_list_free(&errors);
    catalog_free(&available);
    free(catalog_directory);
    free(data);
    return clean ? 0 : 1;
}
