#include "setup_merge.h"

#include "text.h"

#include <stdlib.h>

static const char *const REMOVABLE_LISTS[] = { "apps", "window_managers", "extra_aur", "flatpaks", "cursor_extensions" };

const char *const SETUP_REMOVE_KEYS[] = { "apps", "window_managers", "extra_aur", "flatpaks", "cursor_extensions",
                                          "extra_packages", "user_groups" };
const size_t SETUP_REMOVE_KEY_COUNT = COUNT_OF(SETUP_REMOVE_KEYS);

static bool is_container(const document_node *node) {
    return node->kind == DOCUMENT_TABLE || node->kind == DOCUMENT_ARRAY;
}

static const char *article_for(document_kind kind) {
    return kind == DOCUMENT_ARRAY || kind == DOCUMENT_INTEGER ? "an" : "a";
}

static bool array_contains(const document_node *array, const document_node *item) {
    for (size_t index = 0; index < array->child_count; index++) {
        if (document_equal(array->children[index], item)) {
            return true;
        }
    }
    return false;
}

static document_node *merge_arrays(const document_node *base, const document_node *child) {
    document_node *merged = document_new_array(child->file, child->line);
    const document_node *sources[] = { base, child };
    for (size_t source = 0; source < COUNT_OF(sources); source++) {
        for (size_t index = 0; index < sources[source]->child_count; index++) {
            const document_node *item = sources[source]->children[index];
            if (!array_contains(merged, item)) {
                document_array_append(merged, document_clone(item));
            }
        }
    }
    return merged;
}

static document_node *merge_tables(const document_node *base, const document_node *child, error_list *errors) {
    document_node *merged = document_clone(base);
    for (size_t index = 0; index < child->child_count; index++) {
        const document_node *own = child->children[index];
        const document_node *inherited = document_get(merged, own->key);
        document_node *value = inherited == NULL ? document_clone(own) : setup_merge(inherited, own, errors);
        if (value == NULL) {
            document_free(merged);
            return NULL;
        }
        document_table_set(merged, own->key, value);
    }
    return merged;
}

document_node *setup_merge(const document_node *base, const document_node *child, error_list *errors) {
    if (base->kind == DOCUMENT_TABLE && child->kind == DOCUMENT_TABLE) {
        return merge_tables(base, child, errors);
    }
    if (base->kind == DOCUMENT_ARRAY && child->kind == DOCUMENT_ARRAY) {
        return merge_arrays(base, child);
    }
    if (is_container(base) || is_container(child)) {
        document_error(errors, child, "is %s %s here but %s %s in %s, which this setup extends",
                       article_for(child->kind), document_kind_name(child->kind),
                       article_for(base->kind), document_kind_name(base->kind),
                       base->file == NULL ? "the base" : base->file);
        return NULL;
    }
    return document_clone(child);
}

static void remove_items(document_node *array, const document_node *dropped) {
    if (array == NULL || array->kind != DOCUMENT_ARRAY || dropped == NULL || dropped->kind != DOCUMENT_ARRAY) {
        return;
    }
    for (size_t index = array->child_count; index > 0; index--) {
        if (array_contains(dropped, array->children[index - 1])) {
            document_array_remove_at(array, index - 1);
        }
    }
}

static void apply_remove(document_node *merged, const document_node *remove) {
    for (size_t index = 0; index < COUNT_OF(REMOVABLE_LISTS); index++) {
        remove_items(document_get_mutable(merged, REMOVABLE_LISTS[index]), document_get(remove, REMOVABLE_LISTS[index]));
    }
    const document_node *packages = document_get(remove, "extra_packages");
    document_node *merged_packages = document_get_mutable(merged, "extra_packages");
    for (size_t index = 0; packages != NULL && packages->kind == DOCUMENT_TABLE && index < packages->child_count; index++) {
        const document_node *family = packages->children[index];
        remove_items(document_get_mutable(merged_packages, family->key), family);
    }
    remove_items(document_get_mutable(document_get_mutable(merged, "system"), "user_groups"),
                 document_get(remove, "user_groups"));
}

void setup_apply_removes(document_node *merged, const document_node *removes) {
    for (size_t index = 0; index < removes->child_count; index++) {
        apply_remove(merged, removes->children[index]);
    }
}
