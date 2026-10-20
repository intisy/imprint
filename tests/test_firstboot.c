/** @implNote lstat and readlink are POSIX, hidden by a strict -std=c17 unless asked for. */
#define _XOPEN_SOURCE 700

#include "file_tree.h"
#include "firstboot.h"
#include "platform.h"
#include "text.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

#define GOLDEN IMPRINT_TEST_FIXTURES "/firstboot"
#define HOOKS_DATA IMPRINT_TEST_FIXTURES "/hooks/data"

static char *read_text(const char *path) {
    error_list errors;
    error_list_init(&errors);
    char *text = NULL;
    if (!file_read_all(path, &text, &errors)) {
        error_list_print(&errors, stderr);
    }
    error_list_free(&errors);
    return text;
}

static void hooked_scripts(string_list *scripts) {
    string_list_init(scripts);
    string_list_add(scripts, "hooks/first.sh");
    string_list_add(scripts, "hooks/second.sh");
}

UTEST(firstboot, the_unit_matches_the_golden_file) {
    char *expected = read_text(GOLDEN "/imprint-firstboot.service");
    char *unit = firstboot_unit_text("hooked");
    ASSERT_TRUE(expected != NULL);
    ASSERT_STREQ(expected, unit);
    free(unit);
    free(expected);
}

UTEST(firstboot, the_runner_matches_the_golden_file) {
    string_list scripts;
    hooked_scripts(&scripts);
    char *expected = read_text(GOLDEN "/run.sh");
    char *runner = firstboot_runner_text("hooked", &scripts);
    ASSERT_TRUE(expected != NULL);
    ASSERT_STREQ(expected, runner);
    free(runner);
    free(expected);
    string_list_free(&scripts);
}

#ifndef _WIN32

#include <sys/stat.h>
#include <unistd.h>

#define WORK IMPRINT_TEST_WORK

static char *fresh_directory(const char *name) {
    char *path = path_join(WORK, name);
    const char *const remove_arguments[] = { "rm", "-rf", path, NULL };
    char *ignored = NULL;
    error_list errors;
    error_list_init(&errors);
    platform_run_capture(remove_arguments, &ignored, &errors);
    directory_make_all(path, &errors);
    error_list_free(&errors);
    free(ignored);
    return path;
}

static bool exists_in(const char *root, const char *relative) {
    char *path = path_join(root, relative);
    struct stat information;
    bool exists = lstat(path, &information) == 0;
    free(path);
    return exists;
}

static unsigned int mode_in(const char *root, const char *relative) {
    char *path = path_join(root, relative);
    struct stat information;
    unsigned int mode = stat(path, &information) == 0 ? (unsigned int)(information.st_mode & 07777) : 0;
    free(path);
    return mode;
}

static bool file_in_matches(const char *root, const char *relative, const char *golden) {
    char *path = path_join(root, relative);
    char *actual = read_text(path);
    char *expected = read_text(golden);
    bool matches = actual != NULL && expected != NULL && strcmp(actual, expected) == 0;
    free(expected);
    free(actual);
    free(path);
    return matches;
}

static bool install(const char *target, const char *personal, bool *installed, hook_list *hooks, error_list *errors) {
    hook_list_init(hooks);
    document_node *merged = document_parse_json(
        "{\"hooks\": [{\"stage\": \"firstboot\", \"script\": \"hooks/first.sh\"},"
        " {\"stage\": \"session\", \"script\": \"hooks/build.sh\"},"
        " {\"stage\": \"firstboot\", \"script\": \"hooks/second.sh\"}]}",
        "hooked.json", errors);
    bool read = merged != NULL && hooks_read(merged, hooks, errors);
    document_free(merged);
    firstboot_request request = { .target_root = target, .setup_name = "hooked", .setup_json = "{\"name\": \"hooked\"}\n",
                                  .hooks = hooks, .data_directory = HOOKS_DATA, .personal_directory = personal };
    return read && firstboot_install(&request, installed, errors);
}

UTEST(firstboot, install_writes_the_enabled_unit_and_the_bundle) {
    char *target = fresh_directory("firstboot-target");
    hook_list hooks;
    error_list errors;
    error_list_init(&errors);
    bool installed = false;
    ASSERT_TRUE(install(target, IMPRINT_TEST_FIXTURES "/hooks/personal", &installed, &hooks, &errors));
    error_list_print(&errors, stderr);
    ASSERT_TRUE(installed);
    ASSERT_TRUE(file_in_matches(target, "etc/systemd/system/imprint-firstboot.service", GOLDEN "/imprint-firstboot.service"));
    char *link_path = path_join(target, "etc/systemd/system/multi-user.target.wants/imprint-firstboot.service");
    char link_target[256] = { 0 };
    ASSERT_TRUE(readlink(link_path, link_target, sizeof link_target - 1) > 0);
    ASSERT_STREQ("/etc/systemd/system/imprint-firstboot.service", link_target);
    ASSERT_TRUE(file_in_matches(target, "var/lib/imprint/firstboot/run.sh", GOLDEN "/run.sh"));
    ASSERT_EQ(0755u, mode_in(target, "var/lib/imprint/firstboot/run.sh"));
    ASSERT_EQ(0700u, mode_in(target, "var/lib/imprint/firstboot"));
    ASSERT_EQ(0600u, mode_in(target, "var/lib/imprint/firstboot/setup.json"));
    ASSERT_EQ(0700u, mode_in(target, "var/lib/imprint/firstboot/personal"));
    ASSERT_TRUE(exists_in(target, "var/lib/imprint/firstboot/data/setups/hooked.toml"));
    ASSERT_TRUE(exists_in(target, "var/lib/imprint/firstboot/data/hooks/lib/helper.txt"));
    ASSERT_FALSE(exists_in(target, "var/lib/imprint/firstboot/data/hooks/.hidden"));
    ASSERT_TRUE(exists_in(target, "var/lib/imprint/firstboot/personal/tailscale.key"));
    free(link_path);
    hook_list_free(&hooks);
    error_list_free(&errors);
    free(target);
}

UTEST(firstboot, installing_twice_replaces_the_first_install) {
    char *target = fresh_directory("firstboot-twice");
    hook_list hooks;
    error_list errors;
    error_list_init(&errors);
    bool installed = false;
    ASSERT_TRUE(install(target, NULL, &installed, &hooks, &errors));
    hook_list_free(&hooks);
    ASSERT_TRUE(install(target, NULL, &installed, &hooks, &errors));
    error_list_print(&errors, stderr);
    ASSERT_TRUE(installed);
    ASSERT_TRUE(exists_in(target, "var/lib/imprint/firstboot/personal"));
    hook_list_free(&hooks);
    error_list_free(&errors);
    free(target);
}

UTEST(firstboot, a_setup_without_firstboot_hooks_writes_nothing) {
    char *target = fresh_directory("firstboot-nothing");
    hook_list hooks;
    hook_list_init(&hooks);
    error_list errors;
    error_list_init(&errors);
    firstboot_request request = { .target_root = target, .setup_name = "plain", .setup_json = "{}",
                                  .hooks = &hooks, .data_directory = HOOKS_DATA, .personal_directory = NULL };
    bool installed = true;
    ASSERT_TRUE(firstboot_install(&request, &installed, &errors));
    ASSERT_FALSE(installed);
    ASSERT_FALSE(exists_in(target, "etc"));
    ASSERT_FALSE(exists_in(target, "var"));
    error_list_free(&errors);
    free(target);
}

UTEST(firstboot, a_missing_target_root_is_refused_before_anything_is_written) {
    hook_list hooks;
    error_list errors;
    error_list_init(&errors);
    bool installed = true;
    ASSERT_FALSE(install(WORK "/no-such-root", NULL, &installed, &hooks, &errors));
    ASSERT_FALSE(installed);
    ASSERT_TRUE(error_list_mentions(&errors, "the target root is not a directory"));
    hook_list_free(&hooks);
    error_list_free(&errors);
}

UTEST(firstboot, the_runner_is_valid_shell) {
    const char *const arguments[] = { "sh", "-n", GOLDEN "/run.sh", NULL };
    char *output = NULL;
    error_list errors;
    error_list_init(&errors);
    ASSERT_EQ(0, platform_run_capture(arguments, &output, &errors));
    free(output);
    error_list_free(&errors);
}

#endif
