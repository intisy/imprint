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
bool setup_load(const char *data_directory, const char *name, setup *loaded, error_list *errors);
void setup_free(setup *loaded);

#endif
