#ifndef IMPRINT_WORKSPACE_H
#define IMPRINT_WORKSPACE_H

#include "error.h"

#include <stdbool.h>

typedef struct workspace {
    char *catalog_directory;
    char *data_directory;
} workspace;

/**
 * Always fills the paths, so the caller closes it either way. False means the checkout is unusable, with one
 * error per missing submodule and the git command that fixes it, decided from the directories alone.
 */
bool workspace_open(const char *root, workspace *opened, error_list *errors);
void workspace_close(workspace *opened);

#endif
