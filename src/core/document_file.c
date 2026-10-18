#include "document.h"

#include "text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_stream(FILE *stream) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = memory_allocate(capacity);
    size_t read = 0;
    while ((read = fread(buffer + length, 1, capacity - length - 1, stream)) > 0) {
        length += read;
        if (length + 1 == capacity) {
            capacity *= 2;
            buffer = memory_reallocate(buffer, capacity);
        }
    }
    if (ferror(stream) != 0) {
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}

bool file_read_all(const char *path, char **text, error_list *errors) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        error_push(errors, path, 0, "cannot read: %s", strerror(errno));
        return false;
    }
    char *buffer = read_stream(stream);
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
