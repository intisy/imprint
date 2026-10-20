#include "file_tree.h"
#include "storage_choice.h"
#include "text.h"
#include "utest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *text_of(const document_node *table, const char *key) {
    const document_node *found = document_get(table, key);
    return found != NULL && found->kind == DOCUMENT_STRING ? found->string_value : NULL;
}

static bool is_refused(bool (*check)(const char *, error_list *), const char *value, const char *message) {
    error_list errors;
    error_list_init(&errors);
    bool refused = !check(value, &errors) && error_list_mentions(&errors, message);
    error_list_free(&errors);
    return refused;
}

static bool threshold_check(const char *text, error_list *errors) {
    long long threshold = -1;
    return storage_threshold_parse(text, &threshold, errors);
}

UTEST(storage_choice, a_data_partition_makes_the_default_node_and_the_key_the_default_join) {
    storage_choice choice;
    storage_choice_init(&choice);
    storage_choice_follow_defaults(&choice, true);
    ASSERT_EQ(STORAGE_NODE_DATA_DISK, choice.node);
    ASSERT_EQ(50LL, choice.threshold);
    ASSERT_FALSE(choice.join);
    choice.tailscale_key_on_stick = true;
    storage_choice_follow_defaults(&choice, true);
    ASSERT_TRUE(choice.join);
    storage_choice_follow_defaults(&choice, false);
    ASSERT_EQ(STORAGE_NODE_NONE, choice.node);
    ASSERT_FALSE(choice.join);
    storage_choice_free(&choice);
}

UTEST(storage_choice, a_changed_node_and_join_stay_until_the_data_partition_goes) {
    storage_choice choice;
    storage_choice_init(&choice);
    choice.tailscale_key_on_stick = true;
    choice.node = STORAGE_NODE_NONE;
    choice.node_edited = true;
    storage_choice_follow_defaults(&choice, true);
    ASSERT_EQ(STORAGE_NODE_NONE, choice.node);
    choice.node = STORAGE_NODE_DATA_DISK;
    choice.join = false;
    choice.join_edited = true;
    storage_choice_follow_defaults(&choice, true);
    ASSERT_FALSE(choice.join);
    storage_choice_follow_defaults(&choice, false);
    ASSERT_EQ(STORAGE_NODE_NONE, choice.node);
    choice.node = STORAGE_NODE_FOLDER;
    storage_choice_follow_defaults(&choice, false);
    ASSERT_EQ(STORAGE_NODE_FOLDER, choice.node);
    storage_choice_free(&choice);
}

UTEST(storage_choice, the_threshold_is_a_whole_number_from_0_to_100) {
    long long threshold = -1;
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(storage_threshold_parse("0", &threshold, &errors));
    ASSERT_EQ(0LL, threshold);
    ASSERT_TRUE(storage_threshold_parse("100", &threshold, &errors));
    ASSERT_EQ(100LL, threshold);
    error_list_free(&errors);
    ASSERT_TRUE(is_refused(threshold_check, "101", "from 0 to 100, not '101'"));
    ASSERT_TRUE(is_refused(threshold_check, "-1", "not '-1'"));
    ASSERT_TRUE(is_refused(threshold_check, "50%", "not '50%'"));
    ASSERT_TRUE(is_refused(threshold_check, "", "not ''"));
}

UTEST(storage_choice, the_bands_follow_the_threshold) {
    ASSERT_STREQ("keeps EVERYTHING, backups included", storage_threshold_bands(0));
    ASSERT_STREQ("keeps essential, working and archive", storage_threshold_bands(1));
    ASSERT_STREQ("keeps essential and working", storage_threshold_bands(50));
    ASSERT_STREQ("keeps essential and working", storage_threshold_bands(79));
    ASSERT_STREQ("keeps essential", storage_threshold_bands(80));
}

UTEST(storage_choice, a_folder_is_absolute_and_safe_for_a_shell) {
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(storage_folder_path_check("/srv/storage", &errors));
    error_list_free(&errors);
    ASSERT_TRUE(is_refused(storage_folder_path_check, "srv/storage", "absolute path below /"));
    ASSERT_TRUE(is_refused(storage_folder_path_check, "/", "absolute path below /"));
    ASSERT_TRUE(is_refused(storage_folder_path_check, "/my files", "a space, a quote"));
    ASSERT_TRUE(is_refused(storage_folder_path_check, "/srv/$HOME", "a space, a quote"));
}

UTEST(storage_choice, either_key_file_on_the_stick_counts) {
    char *personal = path_join(IMPRINT_TEST_WORK, "storage-personal");
    char *key = path_join(personal, "tailscale-authkey");
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(directory_make_all(personal, &errors));
    remove(key);
    ASSERT_FALSE(storage_tailscale_key_present(personal));
    ASSERT_FALSE(storage_tailscale_key_present(NULL));
    ASSERT_TRUE(file_write_text(key, "tskey-test\n", &errors));
    ASSERT_TRUE(storage_tailscale_key_present(personal));
    error_list_free(&errors);
    free(key);
    free(personal);
}

UTEST(storage_choice, the_saved_document_names_the_root_and_joins_only_with_a_node) {
    storage_choice choice;
    storage_choice_init(&choice);
    choice.node = STORAGE_NODE_FOLDER;
    choice.folder_path = text_duplicate("/srv/storage");
    choice.threshold = 80;
    choice.join = true;
    document_node *saved = storage_choice_to_document(&choice, "/data");
    ASSERT_STREQ("folder", text_of(saved, "node"));
    ASSERT_STREQ("/srv/storage", text_of(saved, "path"));
    ASSERT_EQ(80LL, document_get(saved, "threshold")->integer_value);
    ASSERT_TRUE(document_get(saved, "join")->boolean_value);
    document_free(saved);
    choice.node = STORAGE_NODE_NONE;
    saved = storage_choice_to_document(&choice, "/data");
    ASSERT_STREQ("none", text_of(saved, "node"));
    ASSERT_TRUE(document_get(saved, "path") == NULL);
    ASSERT_FALSE(document_get(saved, "join")->boolean_value);
    char *json = document_to_json(saved);
    ASSERT_TRUE(strstr(json, "\"threshold\":\t80") != NULL);
    free(json);
    document_free(saved);
    storage_choice_free(&choice);
}
