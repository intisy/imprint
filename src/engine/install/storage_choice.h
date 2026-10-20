#ifndef IMPRINT_STORAGE_CHOICE_H
#define IMPRINT_STORAGE_CHOICE_H

#include "document.h"

#include <stdbool.h>

enum { STORAGE_DEFAULT_THRESHOLD = 50, STORAGE_MAXIMUM_THRESHOLD = 100 };

typedef enum storage_node { STORAGE_NODE_NONE, STORAGE_NODE_DATA_DISK, STORAGE_NODE_FOLDER } storage_node;

/**
 * The review screen's "Storage node" section, saved for the target as `choices.storage` (connect-devices design,
 * "Choosing the storage node at install").
 *
 * @implNote node and join follow their defaults (the data partition, the stick's Tailscale key) until the person
 * changes them; a data-disk node always falls back to none once the data partition is turned off.
 */
typedef struct storage_choice {
    storage_node node;
    char *folder_path;
    long long threshold;
    bool join;
    bool node_edited;
    bool join_edited;
    bool tailscale_key_on_stick;
} storage_choice;

void storage_choice_init(storage_choice *choice);
void storage_choice_free(storage_choice *choice);

void storage_choice_follow_defaults(storage_choice *choice, bool has_data_partition);

/** True when personal/ holds `tailscale.key` or `tailscale-authkey`, the files the storage-join hook reads. */
bool storage_tailscale_key_present(const char *personal_directory);

const char *storage_node_name(storage_node node);

/** The bands a threshold keeps, for the review screen: essential 80+, working 50+, archive 1+, 0 everything. */
const char *storage_threshold_bands(long long threshold);

/** A whole number from 0 to 100; 0 still needs the person's confirmation, which the caller asks for. */
bool storage_threshold_parse(const char *text, long long *threshold, error_list *errors);

/** An absolute path below the root, without whitespace or quotes, since the hook passes it to a shell. */
bool storage_folder_path_check(const char *path, error_list *errors);

/** The node's root on the installed system: the data partition's mount point, the folder, or NULL for none. */
const char *storage_choice_root(const storage_choice *choice, const char *data_mount);

/** `{"node", "path", "threshold", "join"}`; `path` only for a node, and `join` is false without one. */
document_node *storage_choice_to_document(const storage_choice *choice, const char *data_mount);

#endif
