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

UTEST(install_run, a_blank_disk_installs_in_order_with_both_grub_paths) {
    run_fixture fixture;
    fixture_open(&fixture, "run-blank");
    const char *const answers[] = { "5", "pass word", "pass word", "1" };
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
    const char *const answers[] = { "2", "2", "5", "pw", "pw", "1", "sdX" };
    ui *screen = NULL;
    error_list errors;
    error_list_init(&errors);
    int exit_code = run_with(&fixture, answers, COUNT_OF(answers), &screen, &errors);
    ASSERT_EQ(IMPRINT_EXIT_USAGE, exit_code);
    ASSERT_TRUE(error_list_mentions(&errors, "/dev/sdb is not empty and its name was not typed"));
    ASSERT_TRUE(strstr(ui_scripted_transcript(screen), "labelled as a data partition") != NULL);
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

UTEST(install_run, a_disk_swapped_after_the_review_is_refused) {
    run_fixture fixture;
    fixture_open(&fixture, "run-swapped");
    char *swapped = text_duplicate(fixture.lsblk);
    char *serial = strstr(swapped, "S6P7NF0T123456");
    memcpy(serial, "XXXXXXXXXXXXXX", 14);
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
