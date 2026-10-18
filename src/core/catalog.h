#ifndef IMPRINT_CATALOG_H
#define IMPRINT_CATALOG_H

#include "document.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct catalog_entry {
    char *id;
    char *name;
    char *origin;
    bool overridden;
    document_node *node;
} catalog_entry;

typedef struct entry_set {
    catalog_entry *items;
    size_t count;
    size_t capacity;
} entry_set;

typedef struct catalog {
    entry_set apps;
    entry_set distros;
    entry_set window_managers;
    entry_set bootloaders;
} catalog;

bool catalog_load(const char *catalog_directory, const char *data_directory, catalog *loaded, error_list *errors);
void catalog_free(catalog *loaded);
const catalog_entry *catalog_find(const entry_set *set, const char *id);
const document_node *catalog_app_recipe(const catalog_entry *app, const char *distro);

#endif
