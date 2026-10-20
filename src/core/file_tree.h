#ifndef IMPRINT_FILE_TREE_H
#define IMPRINT_FILE_TREE_H

#include "error.h"

#include <stdbool.h>

/** Creates the directory and every missing parent, as mkdir -p does. */
bool directory_make_all(const char *path, error_list *errors);

/** @implNote Writes in binary mode, so a file written on Windows keeps LF line ends for the Linux target. */
bool file_write_text(const char *path, const char *text, error_list *errors);
bool file_copy(const char *from, const char *to, error_list *errors);

/** Copies a directory into another, recursively, leaving out every name that starts with a dot (.git among them). */
bool file_tree_copy(const char *from, const char *to, error_list *errors);

#endif
