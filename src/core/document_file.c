#include "document.h"

#include "platform.h"
#include "text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool file_read_all(const char *path, char **text, error_list *errors) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        error_push(errors, path, 0, "cannot read: %s", strerror(errno));
        return false;
    }
    char *buffer = platform_read_stream(stream);
    fclose(stream);
    if (buffer == NULL) {
        error_push(errors, path, 0, "cannot read: the read failed");
        return false;
    }
    *text = buffer;
    return true;
}

document_node *document_load_file(const char *path, error_list *errors) {
    bool is_toml = text_ends_with(path, ".toml");
    if (!is_toml && !text_ends_with(path, ".json")) {
        error_push(errors, path, 0, "not a .toml or .json file");
        return NULL;
    }
    char *text = NULL;
    if (!file_read_all(path, &text, errors)) {
        return NULL;
    }
    document_node *root = is_toml ? document_parse_toml(text, path, errors) : document_parse_json(text, path, errors);
    free(text);
    return root;
}
