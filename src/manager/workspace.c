#include "workspace.h"

#include "platform.h"
#include "text.h"

#include <stdlib.h>

static bool has_subdirectory(const char *directory, const char *name) {
    char *path = path_join(directory, name);
    bool found = platform_is_directory(path);
    free(path);
    return found;
}

bool workspace_open(const char *root, workspace *opened, error_list *errors) {
    opened->catalog_directory = path_join(root, "catalog");
    opened->data_directory = path_join(root, "data");
    if (!platform_is_directory(opened->catalog_directory) && !platform_is_directory(opened->data_directory)) {
        error_push(errors, root, 0, "not an imprint checkout: there is no catalog/ or data/ here; run imprint "
                                    "from the checkout or pass --root");
        return false;
    }
    bool valid = true;
    if (!has_subdirectory(opened->catalog_directory, "generated")) {
        error_push(errors, opened->catalog_directory, 0,
                   "the catalog submodule is not checked out (run: git submodule update --init)");
        valid = false;
    }
    if (!has_subdirectory(opened->data_directory, "setups")) {
        error_push(errors, opened->data_directory, 0,
                   "the data submodule is not checked out (run: git submodule update --init)");
        valid = false;
    }
    return valid;
}

void workspace_close(workspace *opened) {
    free(opened->catalog_directory);
    free(opened->data_directory);
    opened->catalog_directory = NULL;
    opened->data_directory = NULL;
}
