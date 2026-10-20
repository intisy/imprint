#include "file_tree.h"

#include "platform.h"
#include "text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_separator(char character) {
    return character == '/' || character == '\\';
}

bool directory_make_all(const char *path, error_list *errors) {
    char *partial = text_duplicate(path);
    size_t length = strlen(partial);
    for (size_t index = 1; index < length; index++) {
        if (!is_separator(partial[index]) || is_separator(partial[index - 1]) || partial[index - 1] == ':') {
            continue;
        }
        partial[index] = '\0';
        bool made = platform_make_directory(partial, errors);
        partial[index] = path[index];
        if (!made) {
            free(partial);
            return false;
        }
    }
    free(partial);
    return platform_make_directory(path, errors);
}

static bool write_bytes(const char *path, const char *bytes, size_t length, error_list *errors) {
    FILE *stream = fopen(path, "wb");
    if (stream == NULL) {
        error_push(errors, path, 0, "cannot write the file: %s", strerror(errno));
        return false;
    }
    bool written = fwrite(bytes, 1, length, stream) == length;
    written = fclose(stream) == 0 && written;
    if (!written) {
        error_push(errors, path, 0, "cannot write the file: %s", strerror(errno));
    }
    return written;
}

bool file_write_text(const char *path, const char *text, error_list *errors) {
    return write_bytes(path, text, strlen(text), errors);
}

bool file_copy(const char *from, const char *to, error_list *errors) {
    FILE *source = fopen(from, "rb");
    if (source == NULL) {
        error_push(errors, from, 0, "cannot read the file: %s", strerror(errno));
        return false;
    }
    FILE *target = fopen(to, "wb");
    if (target == NULL) {
        error_push(errors, to, 0, "cannot write the file: %s", strerror(errno));
        fclose(source);
        return false;
    }
    bool copied = platform_copy_stream(source, target);
    fclose(source);
    copied = fclose(target) == 0 && copied;
    if (!copied) {
        error_push(errors, to, 0, "cannot copy %s here", from);
    }
    return copied;
}

static bool copy_each(const char *from, const char *to, const string_list *names, bool directories,
                      error_list *errors) {
    bool copied = true;
    for (size_t index = 0; copied && index < names->count; index++) {
        char *source = path_join(from, names->items[index]);
        char *target = path_join(to, names->items[index]);
        copied = directories ? file_tree_copy(source, target, errors) : file_copy(source, target, errors);
        free(source);
        free(target);
    }
    return copied;
}

bool file_tree_copy(const char *from, const char *to, error_list *errors) {
    string_list files;
    string_list directories;
    string_list_init(&files);
    string_list_init(&directories);
    bool copied = directory_make_all(to, errors) && platform_list_files(from, &files, errors) &&
                  platform_list_directories(from, &directories, errors) &&
                  copy_each(from, to, &files, false, errors) && copy_each(from, to, &directories, true, errors);
    string_list_free(&files);
    string_list_free(&directories);
    return copied;
}
