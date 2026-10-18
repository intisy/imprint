#include "document.h"

#include "text.h"

#include <stdlib.h>
#include <string.h>

static document_node *node_allocate(document_kind kind, const char *file, int line) {
    document_node *node = memory_allocate(sizeof *node);
    memset(node, 0, sizeof *node);
    node->kind = kind;
    node->file = file == NULL ? NULL : text_duplicate(file);
    node->line = line;
    return node;
}

document_node *document_new_table(const char *file, int line) {
    return node_allocate(DOCUMENT_TABLE, file, line);
}

document_node *document_new_array(const char *file, int line) {
    return node_allocate(DOCUMENT_ARRAY, file, line);
}

document_node *document_new_string(const char *value, const char *file, int line) {
    document_node *node = node_allocate(DOCUMENT_STRING, file, line);
    node->string_value = text_duplicate(value);
    return node;
}

document_node *document_new_integer(long long value, const char *file, int line) {
    document_node *node = node_allocate(DOCUMENT_INTEGER, file, line);
    node->integer_value = value;
    return node;
}

document_node *document_new_float(double value, const char *file, int line) {
    document_node *node = node_allocate(DOCUMENT_FLOAT, file, line);
    node->float_value = value;
    return node;
}

document_node *document_new_boolean(bool value, const char *file, int line) {
    document_node *node = node_allocate(DOCUMENT_BOOLEAN, file, line);
    node->boolean_value = value;
    return node;
}

void document_free(document_node *node) {
    if (node == NULL) {
        return;
    }
    for (size_t index = 0; index < node->child_count; index++) {
        document_free(node->children[index]);
    }
    free(node->children);
    free(node->key);
    free(node->file);
    free(node->string_value);
    free(node);
}

static void children_append(document_node *parent, document_node *child) {
    if (parent->child_count == parent->child_capacity) {
        parent->child_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        parent->children = memory_reallocate(parent->children, parent->child_capacity * sizeof *parent->children);
    }
    child->parent = parent;
    parent->children[parent->child_count++] = child;
}

static bool table_index(const document_node *table, const char *key, size_t *index) {
    for (size_t position = 0; position < table->child_count; position++) {
        if (strcmp(table->children[position]->key, key) == 0) {
            *index = position;
            return true;
        }
    }
    return false;
}

void document_table_set(document_node *table, const char *key, document_node *child) {
    char *owned_key = text_duplicate(key);
    free(child->key);
    child->key = owned_key;
    size_t index = 0;
    if (table_index(table, key, &index)) {
        document_free(table->children[index]);
        child->parent = table;
        table->children[index] = child;
        return;
    }
    children_append(table, child);
}

void document_array_append(document_node *array, document_node *child) {
    free(child->key);
    child->key = NULL;
    children_append(array, child);
}

void document_array_remove_at(document_node *array, size_t index) {
    document_free(array->children[index]);
    memmove(&array->children[index], &array->children[index + 1],
            (array->child_count - index - 1) * sizeof *array->children);
    array->child_count--;
}

bool document_table_remove(document_node *table, const char *key) {
    size_t index = 0;
    if (table == NULL || table->kind != DOCUMENT_TABLE || !table_index(table, key, &index)) {
        return false;
    }
    document_array_remove_at(table, index);
    return true;
}

const document_node *document_get(const document_node *table, const char *key) {
    size_t index = 0;
    if (table == NULL || table->kind != DOCUMENT_TABLE || !table_index(table, key, &index)) {
        return NULL;
    }
    return table->children[index];
}

document_node *document_get_mutable(document_node *table, const char *key) {
    return (document_node *)document_get(table, key);
}

document_node *document_clone(const document_node *node) {
    document_node *copy = node_allocate(node->kind, node->file, node->line);
    copy->key = node->key == NULL ? NULL : text_duplicate(node->key);
    copy->string_value = node->string_value == NULL ? NULL : text_duplicate(node->string_value);
    copy->integer_value = node->integer_value;
    copy->float_value = node->float_value;
    copy->boolean_value = node->boolean_value;
    for (size_t index = 0; index < node->child_count; index++) {
        children_append(copy, document_clone(node->children[index]));
    }
    return copy;
}

static bool tables_equal(const document_node *left, const document_node *right) {
    if (left->child_count != right->child_count) {
        return false;
    }
    for (size_t index = 0; index < left->child_count; index++) {
        const document_node *other = document_get(right, left->children[index]->key);
        if (other == NULL || !document_equal(left->children[index], other)) {
            return false;
        }
    }
    return true;
}

static bool arrays_equal(const document_node *left, const document_node *right) {
    if (left->child_count != right->child_count) {
        return false;
    }
    for (size_t index = 0; index < left->child_count; index++) {
        if (!document_equal(left->children[index], right->children[index])) {
            return false;
        }
    }
    return true;
}

bool document_equal(const document_node *left, const document_node *right) {
    if (left->kind != right->kind) {
        return false;
    }
    switch (left->kind) {
    case DOCUMENT_TABLE:
        return tables_equal(left, right);
    case DOCUMENT_ARRAY:
        return arrays_equal(left, right);
    case DOCUMENT_STRING:
        return strcmp(left->string_value, right->string_value) == 0;
    case DOCUMENT_INTEGER:
        return left->integer_value == right->integer_value;
    case DOCUMENT_FLOAT:
        return left->float_value == right->float_value;
    case DOCUMENT_BOOLEAN:
        return left->boolean_value == right->boolean_value;
    }
    return false;
}

const char *document_kind_name(document_kind kind) {
    switch (kind) {
    case DOCUMENT_TABLE:
        return "table";
    case DOCUMENT_ARRAY:
        return "array";
    case DOCUMENT_STRING:
        return "string";
    case DOCUMENT_INTEGER:
        return "integer";
    case DOCUMENT_FLOAT:
        return "float";
    case DOCUMENT_BOOLEAN:
        return "boolean";
    }
    return "value";
}

static size_t position_in_parent(const document_node *node) {
    for (size_t index = 0; index < node->parent->child_count; index++) {
        if (node->parent->children[index] == node) {
            return index;
        }
    }
    return 0;
}

char *document_path(const document_node *node) {
    if (node == NULL || node->parent == NULL) {
        return text_duplicate("");
    }
    char *parent_path = document_path(node->parent);
    char *path = NULL;
    if (node->parent->kind == DOCUMENT_ARRAY) {
        path = text_format("%s[%zu]", parent_path, position_in_parent(node));
    } else if (parent_path[0] == '\0') {
        path = text_duplicate(node->key);
    } else {
        path = text_format("%s.%s", parent_path, node->key);
    }
    free(parent_path);
    return path;
}

static int nearest_line(const document_node *node) {
    for (; node != NULL; node = node->parent) {
        if (node->line > 0) {
            return node->line;
        }
    }
    return 0;
}

void document_error(error_list *errors, const document_node *node, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *message = text_format_arguments(format, arguments);
    va_end(arguments);
    char *path = document_path(node);
    if (path[0] == '\0') {
        error_push(errors, node->file, nearest_line(node), "%s", message);
    } else {
        error_push(errors, node->file, nearest_line(node), "%s: %s", path, message);
    }
    free(path);
    free(message);
}

static bool wrong_kind(error_list *errors, const document_node *value, const char *expected) {
    document_error(errors, value, "must be %s, found %s", expected, document_kind_name(value->kind));
    return false;
}

bool document_require_string(const document_node *table, const char *key, const char **value, error_list *errors) {
    const document_node *found = document_get(table, key);
    if (found == NULL) {
        document_error(errors, table, "missing required key '%s'", key);
        return false;
    }
    if (found->kind != DOCUMENT_STRING) {
        return wrong_kind(errors, found, "a string");
    }
    *value = found->string_value;
    return true;
}

bool document_optional_string(const document_node *table, const char *key, const char *fallback,
                              const char **value, error_list *errors) {
    const document_node *found = document_get(table, key);
    if (found == NULL) {
        *value = fallback;
        return true;
    }
    if (found->kind != DOCUMENT_STRING) {
        return wrong_kind(errors, found, "a string");
    }
    *value = found->string_value;
    return true;
}

bool document_optional_integer(const document_node *table, const char *key, long long fallback,
                               long long *value, error_list *errors) {
    const document_node *found = document_get(table, key);
    if (found == NULL) {
        *value = fallback;
        return true;
    }
    if (found->kind != DOCUMENT_INTEGER) {
        return wrong_kind(errors, found, "an integer");
    }
    *value = found->integer_value;
    return true;
}

bool document_optional_boolean(const document_node *table, const char *key, bool fallback, bool *value,
                               error_list *errors) {
    const document_node *found = document_get(table, key);
    if (found == NULL) {
        *value = fallback;
        return true;
    }
    if (found->kind != DOCUMENT_BOOLEAN) {
        return wrong_kind(errors, found, "a boolean");
    }
    *value = found->boolean_value;
    return true;
}

bool document_optional_table(const document_node *table, const char *key, const document_node **value,
                             error_list *errors) {
    const document_node *found = document_get(table, key);
    *value = NULL;
    if (found == NULL) {
        return true;
    }
    if (found->kind != DOCUMENT_TABLE) {
        return wrong_kind(errors, found, "a table");
    }
    *value = found;
    return true;
}

bool document_string_list(const document_node *table, const char *key, string_list *values, error_list *errors) {
    const document_node *found = document_get(table, key);
    if (found == NULL) {
        return true;
    }
    if (found->kind != DOCUMENT_ARRAY) {
        return wrong_kind(errors, found, "an array of strings");
    }
    bool valid = true;
    for (size_t index = 0; index < found->child_count; index++) {
        const document_node *item = found->children[index];
        if (item->kind != DOCUMENT_STRING) {
            valid = wrong_kind(errors, item, "a string");
            continue;
        }
        string_list_add(values, item->string_value);
    }
    return valid;
}

static bool is_allowed(const char *key, const char *const *allowed, size_t allowed_count) {
    for (size_t index = 0; index < allowed_count; index++) {
        if (strcmp(key, allowed[index]) == 0) {
            return true;
        }
    }
    return false;
}

bool document_check_keys(const document_node *table, const char *const *allowed, size_t allowed_count,
                         error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < table->child_count; index++) {
        if (!is_allowed(table->children[index]->key, allowed, allowed_count)) {
            document_error(errors, table->children[index], "unknown key");
            valid = false;
        }
    }
    return valid;
}

const char *document_skip_byte_order_mark(const char *text) {
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        return text + 3;
    }
    return text;
}
