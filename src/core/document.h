#ifndef IMPRINT_DOCUMENT_H
#define IMPRINT_DOCUMENT_H

#include "error.h"
#include "string_list.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum document_kind {
    DOCUMENT_TABLE,
    DOCUMENT_ARRAY,
    DOCUMENT_STRING,
    DOCUMENT_INTEGER,
    DOCUMENT_FLOAT,
    DOCUMENT_BOOLEAN
} document_kind;

typedef struct document_node document_node;

struct document_node {
    document_kind kind;
    char *key;
    char *file;
    int line;
    document_node *parent;
    document_node **children;
    size_t child_count;
    size_t child_capacity;
    char *string_value;
    long long integer_value;
    double float_value;
    bool boolean_value;
};

document_node *document_new_table(const char *file, int line);
document_node *document_new_array(const char *file, int line);
document_node *document_new_string(const char *value, const char *file, int line);
document_node *document_new_integer(long long value, const char *file, int line);
document_node *document_new_float(double value, const char *file, int line);
document_node *document_new_boolean(bool value, const char *file, int line);
void document_free(document_node *node);

void document_table_set(document_node *table, const char *key, document_node *child);
void document_array_append(document_node *array, document_node *child);
bool document_table_remove(document_node *table, const char *key);
void document_array_remove_at(document_node *array, size_t index);

const document_node *document_get(const document_node *table, const char *key);
document_node *document_get_mutable(document_node *table, const char *key);
document_node *document_clone(const document_node *node);
bool document_equal(const document_node *left, const document_node *right);
const char *document_kind_name(document_kind kind);
char *document_path(const document_node *node);

void document_error(error_list *errors, const document_node *node, const char *format, ...) IMPRINT_PRINTF(3, 4);

bool document_require_string(const document_node *table, const char *key, const char **value, error_list *errors);
bool document_optional_string(const document_node *table, const char *key, const char *fallback,
                              const char **value, error_list *errors);
bool document_optional_integer(const document_node *table, const char *key, long long fallback,
                               long long *value, error_list *errors);
bool document_optional_boolean(const document_node *table, const char *key, bool fallback, bool *value,
                               error_list *errors);
bool document_optional_table(const document_node *table, const char *key, const document_node **value,
                             error_list *errors);
bool document_string_list(const document_node *table, const char *key, string_list *values, error_list *errors);
bool document_check_keys(const document_node *table, const char *const *allowed, size_t allowed_count,
                         error_list *errors);

document_node *document_parse_toml(const char *text, const char *file, error_list *errors);
document_node *document_parse_json(const char *text, const char *file, error_list *errors);
char *document_to_json(const document_node *node);
bool file_read_all(const char *path, char **text, error_list *errors);
document_node *document_load_file(const char *path, error_list *errors);

const char *document_skip_byte_order_mark(const char *text);

#endif
