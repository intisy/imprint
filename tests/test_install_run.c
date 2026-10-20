#include "disks.h"
#include "exit_codes.h"
#include "fake_runner.h"
#include "file_tree.h"
#include "install.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

#define INSTALL_FIXTURES IMPRINT_TEST_FIXTURES "/install"

typedef struct run_fixture {
    command_runner runner;
    char *lsblk;
    char *target;
} run_fixture;

static void fixture_open(run_fixture *fixture, const char *name) {
    error_list errors;
    error_list_init(&errors);
    fixture->lsblk = NULL;
    file_read_all(IMPRINT_TEST_FIXTURES "/lsblk/p53.json", &fixture->lsblk, &errors);
    fixture->target = path_join(IMPRINT_TEST_WORK, name);
    directory_make_all(fixture->target, &errors);
    error_list_free(&errors);
    fixture->runner = fake_runner_create(stderr);
    fake_runner_answer(&fixture->runner, "lsblk -J", 0, fixture->lsblk);
    fake_runner_answer(&fixture->runner, "findmnt", 0, "/dev/mapper/stick\n");
    fake_runner_answer(&fixture->runner, "lsblk -n -r -s", 0, "stick dm\nsda1 part\nsda disk\n");
}

static void fixture_close(run_fixture *fixture) {
    fake_runner_free(&fixture->runner);
    free(fixture->lsblk);
    free(fixture->target);
}

static int run_with(run_fixture *fixture, const char *const *answers, size_t count, ui **screen_out,
                    error_list *errors) {
    ui *screen = ui_scripted_create(answers, count);
    install_request request = { .setup_file = INSTALL_FIXTURES "/workstation.json",
                                .data_directory = INSTALL_FIXTURES "/data", .stick_root = "/run/stick",
                                .target_root = fixture->target, .live_gpg_directory = "/nowhere",
                                .screen = screen, .runner = &fixture->runner };
    int exit_code = install_run(&request, errors);
    *screen_out = screen;
    return exit_code;
}

UTEST(install_run, two_disks_install_in_order_with_both_grub_paths) {
    run_fixture fixture;
    fixture_open(&fixture, "run-blank");
    const char *const answers[] = { "5", "pass word", "pass word", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    int exit_code = run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    error_list_print(&errors, stderr);
    ASSERT_EQ(IMPRINT_EXIT_OK, exit_code);
    ASSERT_FALSE(ui_scripted_exhausted(screen));
    long partition = fake_runner_index(&fixture.runner, "sgdisk -Z /dev/nvme0n1");
    long pacstrap = fake_runner_index(&fixture.runner, "pacstrap -K");
    long fstab = fake_runner_index(&fixture.runner, "genfstab -U");
    char *entry = text_format("arch-chroot %s grub-install --target=x86_64-efi --efi-directory=/boot "
                              "--bootloader-id=imprint", fixture.target);
    char *removable = text_format("arch-chroot %s grub-install --target=x86_64-efi --efi-directory=/boot --removable",
                                  fixture.target);
    char *unmount = text_format("umount -R %s", fixture.target);
    ASSERT_TRUE(partition >= 0 && pacstrap > partition && fstab > pacstrap);
    ASSERT_TRUE(fake_runner_ran(&fixture.runner, "sgdisk -n 1:0:0 -t 1:8300 -c 1:data /dev/nvme1n1"));
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "labelled as a data partition") != NULL);
    ASSERT_TRUE(fake_runner_index(&fixture.runner, entry) > fstab);
    ASSERT_TRUE(fake_runner_index(&fixture.runner, removable) > fake_runner_index(&fixture.runner, entry));
    ASSERT_TRUE(fake_runner_index(&fixture.runner, unmount) > fake_runner_index(&fixture.runner, removable));
    free(entry);
    free(removable);
    free(unmount);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, a_busy_target_is_detached_lazily_after_a_sync) {
    run_fixture fixture;
    fixture_open(&fixture, "run-busy");
    char *unmount = text_format("umount -R %s", fixture.target);
    fake_runner_answer(&fixture.runner, unmount, 32, "umount: target is busy.");
    const char *const answers[] = { "5", "pw", "pw", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(IMPRINT_EXIT_OK, run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors));
    char *lazily = text_format("umount -R -l %s", fixture.target);
    ASSERT_TRUE(fake_runner_index(&fixture.runner, lazily) > fake_runner_index(&fixture.runner, "sync"));
    free(lazily);
    free(unmount);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, start_without_a_password_shows_the_screen_again) {
    run_fixture fixture;
    fixture_open(&fixture, "run-no-password");
    const char *const answers[] = { "1", "cancel" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    int exit_code = run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    ASSERT_EQ(IMPRINT_EXIT_USAGE, exit_code);
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "set the user's password first") != NULL);
    ASSERT_TRUE(error_list_mentions(&errors, "cancelled: nothing was changed"));
    ASSERT_EQ(-1L, fake_runner_index(&fixture.runner, "sgdisk"));
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, a_used_disk_needs_its_name_typed) {
    run_fixture fixture;
    fixture_open(&fixture, "run-used");
    const char *const answers[] = { "5", "pw", "pw", "1", "sdX" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    int exit_code = run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    ASSERT_EQ(IMPRINT_EXIT_USAGE, exit_code);
    ASSERT_TRUE(error_list_mentions(&errors, "/dev/nvme1n1 is not empty and its name was not typed"));
    ASSERT_EQ(-1L, fake_runner_index(&fixture.runner, "sgdisk"));
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, the_stick_cannot_be_chosen_on_the_review_screen) {
    run_fixture fixture;
    fixture_open(&fixture, "run-stick");
    const char *const answers[] = { "2", "1", "cancel" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "is the stick this install runs from") != NULL);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, the_system_disk_cannot_also_hold_the_data) {
    run_fixture fixture;
    fixture_open(&fixture, "run-same-disk");
    const char *const answers[] = { "5", "pw", "pw", "10", "6", "cancel" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "is the system disk") != NULL);
    ASSERT_EQ(-1L, fake_runner_index(&fixture.runner, "sgdisk"));
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_run, a_disk_swapped_after_the_review_is_refused) {
    run_fixture fixture;
    fixture_open(&fixture, "run-swapped");
    char *swapped = text_duplicate(fixture.lsblk);
    char *serial = strstr(swapped, "WD1TB0001");
    memcpy(serial, "XXXXXXXXX", 9);
    const char *const answers[] = { "5", "pw", "pw", "1" };
    ui *screen = ui_scripted_create(answers, COUNT_OF(answers));
    error_list errors;
    error_list_init(&errors);
    fake_runner_answer(&fixture.runner, "lsblk -J", 0, swapped);
    fake_runner_answer_times(&fixture.runner, "lsblk -J", 0, fixture.lsblk, 1);
    install_request request = { .setup_file = INSTALL_FIXTURES "/workstation.json",
                                .data_directory = INSTALL_FIXTURES "/data", .stick_root = "/run/stick",
                                .target_root = fixture.target, .live_gpg_directory = "/nowhere",
                                .disk = "/dev/nvme0n1", .screen = screen, .runner = &fixture.runner };
    int exit_code = install_run(&request, &errors);
    ASSERT_NE(IMPRINT_EXIT_OK, exit_code);
    ASSERT_TRUE(error_list_mentions(&errors, "changed since the review"));
    ASSERT_EQ(-1L, fake_runner_index(&fixture.runner, "sgdisk"));
    ui_destroy(screen);
    free(swapped);
    error_list_free(&errors);
    fixture_close(&fixture);
}

/** The fixture setup with a firstboot hook, so the install writes the bundle that carries `choices`. */
static char *hooked_setup_file(void) {
    error_list errors;
    error_list_init(&errors);
    char *json = NULL;
    resolved_setup resolved;
    file_read_all(INSTALL_FIXTURES "/workstation.json", &json, &errors);
    resolved_setup_from_json(json, "workstation.json", &resolved, &errors);
    document_node *hooks = document_new_array(NULL, 0);
    document_node *hook = document_new_table(NULL, 0);
    document_table_set(hook, "stage", document_new_string("firstboot", NULL, 0));
    document_table_set(hook, "script", document_new_string("hooks/storage-join.sh", NULL, 0));
    document_array_append(hooks, hook);
    document_table_set(resolved.setup, "hooks", hooks);
    char *hooked = resolved_setup_to_json(&resolved);
    char *path = path_join(IMPRINT_TEST_WORK, "hooked-workstation.json");
    file_write_text(path, hooked, &errors);
    error_list_print(&errors, stderr);
    free(hooked);
    free(json);
    resolved_setup_free(&resolved);
    error_list_free(&errors);
    return path;
}

static int run_hooked(run_fixture *fixture, const char *personal, const char *const *answers, size_t count,
                      ui **screen_out, error_list *errors) {
    char *setup_file = hooked_setup_file();
    ui *screen = ui_scripted_create(answers, count);
    install_request request = { .setup_file = setup_file, .data_directory = INSTALL_FIXTURES "/data",
                                .stick_root = "/run/stick", .personal_directory = personal,
                                .target_root = fixture->target, .live_gpg_directory = "/nowhere",
                                .screen = screen, .runner = &fixture->runner };
    int exit_code = install_run(&request, errors);
    *screen_out = screen;
    free(setup_file);
    return exit_code;
}

/** `choices.storage` as the target's firstboot bundle carries it; NULL when it is missing. */
static document_node *saved_storage(const run_fixture *fixture) {
    error_list errors;
    error_list_init(&errors);
    char *path = path_join(fixture->target, "var/lib/imprint/firstboot/setup.json");
    char *json = NULL;
    resolved_setup resolved;
    resolved_setup_init(&resolved);
    document_node *storage = NULL;
    if (file_read_all(path, &json, &errors) && resolved_setup_from_json(json, path, &resolved, &errors) &&
        resolved.choices != NULL && document_get(resolved.choices, "storage") != NULL) {
        storage = document_clone(document_get(resolved.choices, "storage"));
    }
    error_list_print(&errors, stderr);
    resolved_setup_free(&resolved);
    free(json);
    free(path);
    error_list_free(&errors);
    return storage;
}

static const char *saved_text(const document_node *storage, const char *key) {
    const document_node *found = document_get(storage, key);
    return found != NULL && found->kind == DOCUMENT_STRING ? found->string_value : "(missing)";
}

static char *personal_with_key(const char *name) {
    char *personal = path_join(IMPRINT_TEST_WORK, name);
    char *key = path_join(personal, "tailscale.key");
    error_list errors;
    error_list_init(&errors);
    directory_make_all(personal, &errors);
    file_write_text(key, "tskey-test\n", &errors);
    error_list_free(&errors);
    free(key);
    return personal;
}

UTEST(install_storage, the_default_node_is_the_data_partition_at_50_saved_for_the_target) {
    run_fixture fixture;
    fixture_open(&fixture, "storage-default");
    const char *const answers[] = { "5", "pw", "pw", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(IMPRINT_EXIT_OK, run_hooked(&fixture, NULL, answers, COUNT_OF(answers), &screen, &errors));
    document_node *storage = saved_storage(&fixture);
    ASSERT_TRUE(storage != NULL);
    ASSERT_STREQ("data-disk", saved_text(storage, "node"));
    ASSERT_STREQ("/data", saved_text(storage, "path"));
    ASSERT_EQ(50LL, document_get(storage, "threshold")->integer_value);
    ASSERT_FALSE(document_get(storage, "join")->boolean_value);
    document_free(storage);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_storage, a_tailscale_key_on_the_stick_makes_join_the_default) {
    run_fixture fixture;
    fixture_open(&fixture, "storage-key");
    char *personal = personal_with_key("storage-key-personal");
    const char *const answers[] = { "5", "pw", "pw", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(IMPRINT_EXIT_OK, run_hooked(&fixture, personal, answers, COUNT_OF(answers), &screen, &errors));
    document_node *storage = saved_storage(&fixture);
    ASSERT_TRUE(storage != NULL);
    ASSERT_TRUE(document_get(storage, "join")->boolean_value);
    document_free(storage);
    free(personal);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_storage, a_folder_and_threshold_0_only_once_confirmed) {
    run_fixture fixture;
    fixture_open(&fixture, "storage-folder");
    const char *const answers[] = { "5", "pw", "pw", "11", "2", "srv", "11", "2", "/srv/storage",
                                    "12", "0", "no", "12", "101", "12", "0", "yes", "13", "yes", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(IMPRINT_EXIT_OK, run_hooked(&fixture, NULL, answers, COUNT_OF(answers), &screen, &errors));
    const char *transcript = ui_scripted_transcript(screen);
    ASSERT_TRUE(strstr(transcript, "the folder is an absolute path below /, not 'srv'") != NULL);
    ASSERT_TRUE(strstr(transcript, "the threshold was not changed") != NULL);
    ASSERT_TRUE(strstr(transcript, "from 0 to 100, not '101'") != NULL);
    ASSERT_TRUE(strstr(transcript, "no personal/tailscale.key on the stick") != NULL);
    document_node *storage = saved_storage(&fixture);
    ASSERT_TRUE(storage != NULL);
    ASSERT_STREQ("folder", saved_text(storage, "node"));
    ASSERT_STREQ("/srv/storage", saved_text(storage, "path"));
    ASSERT_EQ(0LL, document_get(storage, "threshold")->integer_value);
    ASSERT_TRUE(document_get(storage, "join")->boolean_value);
    char *make = text_format("arch-chroot %s mkdir -p /srv/storage", fixture.target);
    char *give = text_format("arch-chroot %s chown finn:finn /srv/storage", fixture.target);
    ASSERT_TRUE(fake_runner_ran(&fixture.runner, make));
    ASSERT_TRUE(fake_runner_ran(&fixture.runner, give));
    free(make);
    free(give);
    document_free(storage);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}

UTEST(install_storage, none_saves_no_path_and_no_join) {
    run_fixture fixture;
    fixture_open(&fixture, "storage-none");
    char *personal = personal_with_key("storage-none-personal");
    const char *const answers[] = { "5", "pw", "pw", "11", "3", "13", "1", "nvme1n1" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(IMPRINT_EXIT_OK, run_hooked(&fixture, personal, answers, COUNT_OF(answers), &screen, &errors));
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "choose a storage node first") != NULL);
    document_node *storage = saved_storage(&fixture);
    ASSERT_TRUE(storage != NULL);
    ASSERT_STREQ("none", saved_text(storage, "node"));
    ASSERT_TRUE(document_get(storage, "path") == NULL);
    ASSERT_FALSE(document_get(storage, "join")->boolean_value);
    document_free(storage);
    free(personal);
    ui_destroy(screen);
    error_list_free(&errors);
    fixture_close(&fixture);
}
