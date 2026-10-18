#ifndef IMPRINT_SETUP_H
#define IMPRINT_SETUP_H

#include "document.h"

typedef struct setup {
    char *name;
    char *file;
    document_node *merged;
    string_list chain;
} setup;

bool setup_list(const char *data_directory, string_list *names, error_list *errors);
char *setup_path(const char *data_directory, const char *name);

/**
 * @implNote Matches the exact file name, because Windows file systems ignore case and would otherwise load
 * Workstation.toml for the name workstation while setup_list refuses it.
 */
bool setup_file_exists(const char *data_directory, const char *name);
bool setup_load(const char *data_directory, const char *name, setup *loaded, error_list *errors);
void setup_free(setup *loaded);

#endif
