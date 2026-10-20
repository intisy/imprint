#include "storage_choice.h"

#include "platform.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

enum { ESSENTIAL_THRESHOLD = 80, WORKING_THRESHOLD = 50 };

static const char *const TAILSCALE_KEY_FILES[] = { "tailscale.key", "tailscale-authkey" };

void storage_choice_init(storage_choice *choice) {
    memset(choice, 0, sizeof *choice);
    choice->threshold = STORAGE_DEFAULT_THRESHOLD;
}

void storage_choice_free(storage_choice *choice) {
    free(choice->folder_path);
    storage_choice_init(choice);
}

void storage_choice_follow_defaults(storage_choice *choice, bool has_data_partition) {
    bool data_disk_gone = choice->node == STORAGE_NODE_DATA_DISK && !has_data_partition;
    if (!choice->node_edited || data_disk_gone) {
        choice->node = has_data_partition ? STORAGE_NODE_DATA_DISK : STORAGE_NODE_NONE;
    }
    if (choice->node == STORAGE_NODE_NONE) {
        choice->join = false;
    } else if (!choice->join_edited) {
        choice->join = choice->tailscale_key_on_stick;
    }
}

bool storage_tailscale_key_present(const char *personal_directory) {
    if (personal_directory == NULL) {
        return false;
    }
    bool present = false;
    for (size_t index = 0; !present && index < COUNT_OF(TAILSCALE_KEY_FILES); index++) {
        char *path = path_join(personal_directory, TAILSCALE_KEY_FILES[index]);
        present = platform_is_file(path);
        free(path);
    }
    return present;
}

const char *storage_node_name(storage_node node) {
    switch (node) {
    case STORAGE_NODE_DATA_DISK: return "data-disk";
    case STORAGE_NODE_FOLDER: return "folder";
    default: return "none";
    }
}

const char *storage_threshold_bands(long long threshold) {
    if (threshold <= 0) {
        return "keeps EVERYTHING, backups included";
    }
    if (threshold >= ESSENTIAL_THRESHOLD) {
        return "keeps essential";
    }
    return threshold >= WORKING_THRESHOLD ? "keeps essential and working" : "keeps essential, working and archive";
}

bool storage_threshold_parse(const char *text, long long *threshold, error_list *errors) {
    char *end = NULL;
    long long value = text == NULL ? -1 : strtoll(text, &end, 10);
    if (text == NULL || end == text || *end != '\0' || value < 0 || value > STORAGE_MAXIMUM_THRESHOLD) {
        error_push(errors, NULL, 0, "the threshold is a whole number from 0 to %d, not '%s'", STORAGE_MAXIMUM_THRESHOLD,
                   text == NULL ? "" : text);
        return false;
    }
    *threshold = value;
    return true;
}

bool storage_folder_path_check(const char *path, error_list *errors) {
    if (path == NULL || path[0] != '/' || path[1] == '\0') {
        error_push(errors, NULL, 0, "the folder is an absolute path below /, not '%s'", path == NULL ? "" : path);
        return false;
    }
    if (strpbrk(path, " \t\n\r'\"\\$`") != NULL) {
        error_push(errors, NULL, 0, "the folder '%s' holds a space, a quote or a shell character", path);
        return false;
    }
    return true;
}

const char *storage_choice_root(const storage_choice *choice, const char *data_mount) {
    switch (choice->node) {
    case STORAGE_NODE_DATA_DISK: return data_mount;
    case STORAGE_NODE_FOLDER: return choice->folder_path;
    default: return NULL;
    }
}

document_node *storage_choice_to_document(const storage_choice *choice, const char *data_mount) {
    document_node *storage = document_new_table(NULL, 0);
    const char *root = storage_choice_root(choice, data_mount);
    document_table_set(storage, "node", document_new_string(storage_node_name(root == NULL ? STORAGE_NODE_NONE
                                                                                           : choice->node), NULL, 0));
    if (root != NULL) {
        document_table_set(storage, "path", document_new_string(root, NULL, 0));
    }
    document_table_set(storage, "threshold", document_new_integer(choice->threshold, NULL, 0));
    document_table_set(storage, "join", document_new_boolean(root != NULL && choice->join, NULL, 0));
    return storage;
}
