#include "document.h"

#include "text.h"
#include "tomlc17.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static document_node *convert_datum(toml_datum_t datum, const char *file, const char *path, error_list *errors);

static int line_in_message(const char *message) {
    const char *marker = strstr(message, "line ");
    int line = 0;
    if (marker != NULL && sscanf(marker + strlen("line "), "%d", &line) == 1) {
        return line;
    }
    return 0;
}

static const char *message_without_line(const char *message) {
    if (strncmp(message, "(line ", strlen("(line ")) == 0) {
        const char *rest = strstr(message, ") ");
        if (rest != NULL) {
            return rest + 2;
        }
    }
    return message;
}

static char *key_path(const char *path, const char *key) {
    return path[0] == '\0' ? text_duplicate(key) : text_format("%s.%s", path, key);
}

static document_node *convert_table(toml_datum_t table, const char *file, const char *path, error_list *errors) {
    document_node *node = document_new_table(file, table.lineno);
    for (int index = 0; index < (int)table.u.tab.size; index++) {
        char *child_path = key_path(path, table.u.tab.key[index]);
        document_node *child = convert_datum(table.u.tab.value[index], file, child_path, errors);
        free(child_path);
        if (child == NULL) {
            document_free(node);
            return NULL;
        }
        document_table_set(node, table.u.tab.key[index], child);
    }
    return node;
}

static document_node *convert_array(toml_datum_t array, const char *file, const char *path, error_list *errors) {
    document_node *node = document_new_array(file, array.lineno);
    for (int index = 0; index < (int)array.u.arr.size; index++) {
        char *item_path = text_format("%s[%d]", path, index);
        document_node *child = convert_datum(array.u.arr.elem[index], file, item_path, errors);
        free(item_path);
        if (child == NULL) {
            document_free(node);
            return NULL;
        }
        document_array_append(node, child);
    }
    return node;
}

static document_node *convert_datum(toml_datum_t datum, const char *file, const char *path, error_list *errors) {
    switch (datum.type) {
    case TOML_STRING:
        return document_new_string(datum.u.s, file, datum.lineno);
    case TOML_INT64:
        return document_new_integer((long long)datum.u.int64, file, datum.lineno);
    case TOML_FP64:
        return document_new_float(datum.u.fp64, file, datum.lineno);
    case TOML_BOOLEAN:
        return document_new_boolean(datum.u.boolean, file, datum.lineno);
    case TOML_ARRAY:
        return convert_array(datum, file, path, errors);
    case TOML_TABLE:
        return convert_table(datum, file, path, errors);
    default:
        error_push(errors, file, datum.lineno, "%s: dates and times are not supported in imprint files", path);
        return NULL;
    }
}

document_node *document_parse_toml(const char *text, const char *file, error_list *errors) {
    text = document_skip_byte_order_mark(text);
    toml_result_t result = toml_parse(text, (int)strlen(text));
    if (!result.ok) {
        error_push(errors, file, line_in_message(result.errmsg), "%s", message_without_line(result.errmsg));
        toml_free(result);
        return NULL;
    }
    document_node *root = convert_datum(result.toptab, file, "", errors);
    toml_free(result);
    return root;
}
