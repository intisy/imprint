#include "install.h"

#include "base_system.h"
#include "bootloader.h"
#include "exit_codes.h"
#include "firstboot.h"
#include "hook.h"
#include "layout.h"
#include "offline.h"
#include "software.h"

#include <stdlib.h>
#include <string.h>

typedef struct install_session {
    const install_request *request;
    char *setup_json;
    resolved_setup resolved;
    install_options options;
    offline_stick stick;
    disk_list disks;
    char *stick_disk;
    string_list problems;
} install_session;

static void session_init(install_session *session, const install_request *request) {
    memset(session, 0, sizeof *session);
    session->request = request;
    resolved_setup_init(&session->resolved);
    install_options_init(&session->options);
    disk_list_init(&session->disks);
    string_list_init(&session->problems);
}

static void session_free(install_session *session) {
    free(session->setup_json);
    resolved_setup_free(&session->resolved);
    install_options_free(&session->options);
    offline_stick_free(&session->stick);
    disk_list_free(&session->disks);
    free(session->stick_disk);
    string_list_free(&session->problems);
}

static bool load_setup(install_session *session, error_list *errors) {
    const install_request *request = session->request;
    if (!file_read_all(request->setup_file, &session->setup_json, errors) ||
        !resolved_setup_from_json(session->setup_json, request->setup_file, &session->resolved, errors) ||
        !install_options_read(&session->resolved, &session->options, errors)) {
        return false;
    }
    if (strcmp(session->resolved.package_manager, "pacman") != 0) {
        error_push(errors, request->setup_file, 0, "disk-install supports Arch setups only, not '%s'",
                   session->resolved.distro);
        return false;
    }
    return true;
}

static void find_stick(install_session *session, error_list *errors) {
    const install_request *request = session->request;
    offline_stick_find(request->stick_root, &session->stick);
    if (request->personal_directory != NULL) {
        install_options_replace(&session->stick.personal, request->personal_directory);
    }
    if (request->stick_root != NULL) {
        session->stick_disk = disks_holding_directory(request->runner, request->stick_root, errors);
    }
    runner_say(request->runner, "stick: offline repository %s, AUR builds %s, Flatpaks %s, personal folder %s",
               session->stick.repository != NULL ? "found" : "absent",
               session->stick.aur_repository != NULL ? "found" : "absent",
               session->stick.flatpak_export != NULL ? "found" : "absent",
               session->stick.personal != NULL ? session->stick.personal : "absent");
}

static unsigned long long system_minimum_bytes(const install_options *options) {
    enum { DEFAULT_ROOT_MINIMUM_MB = 65536 };
    long long root = options->offered_root_size_mb > 0 ? options->offered_root_size_mb : DEFAULT_ROOT_MINIMUM_MB;
    return (unsigned long long)(options->esp_size_mb + options->swap_size_mb + root) * 1024ULL * 1024ULL;
}

/** With a data partition and a second internal disk, the system goes on the smaller and the data on the larger. */
static void choose_default_disks(install_session *session) {
    install_options *options = &session->options;
    const disk *system = NULL;
    const disk *data = NULL;
    if (session->request->disk == NULL) {
        disks_default_pair(&session->disks, session->stick_disk, system_minimum_bytes(options), &system, &data);
    }
    install_options_replace(&options->disk, session->request->disk != NULL ? session->request->disk
                                            : system == NULL ? NULL : system->path);
    const char *data_disk = session->request->data_disk != NULL ? session->request->data_disk
                            : data == NULL ? NULL : data->path;
    if (data_disk != NULL && options->data_label != NULL) {
        install_options_use_data_disk(options, data_disk);
    }
}

/** @implNote The disk list is read again right before writing, so a disk swapped during the review is refused. */
static bool confirm_erase(install_session *session, const char *path, error_list *errors) {
    const disk *reviewed = disks_find(&session->disks, path);
    if (!install_confirm_erase(session->request->screen, reviewed, session->options.offered_data_label)) {
        error_push(errors, NULL, 0, "%s is not empty and its name was not typed: nothing was changed", path);
        return false;
    }
    return true;
}

static bool unchanged_since_review(install_session *session, const disk_list *now, const char *path,
                                   error_list *errors) {
    bool same = disk_same_identity(disks_find(&session->disks, path), disks_find(now, path)) &&
                disk_check_target(now, path, session->stick_disk, errors);
    if (!same) {
        error_push(errors, NULL, 0, "%s changed since the review (another disk, serial or size): nothing was changed",
                   path);
    }
    return same;
}

static bool confirm_target(install_session *session, error_list *errors) {
    const char *data_disk = session->options.data_disk;
    if (!confirm_erase(session, session->options.disk, errors) ||
        (data_disk != NULL && !confirm_erase(session, data_disk, errors))) {
        return false;
    }
    disk_list now;
    disk_list_init(&now);
    bool same = disks_read(session->request->runner, &now, errors) &&
                unchanged_since_review(session, &now, session->options.disk, errors) &&
                (data_disk == NULL || unchanged_since_review(session, &now, data_disk, errors));
    disk_list_free(&now);
    return same;
}

static void print_plan(install_session *session) {
    command_runner *runner = session->request->runner;
    const install_options *options = &session->options;
    char *layout = layout_describe(options);
    runner_say(runner, "=== install plan for setup '%s' ===", session->resolved.name);
    runner_say(runner, "disk %s: %s", options->disk, layout);
    runner_say(runner, "hostname %s, user %s, time zone %s, locale %s", options->hostname, options->username,
               options->timezone, options->locale);
    runner_say(runner, "bootloader %s, display manager %s", options->bootloader,
               install_options_have_display_manager(options) ? options->display_manager : "none");
    free(layout);
}

static bool prepare_disk(install_session *session, error_list *errors) {
    command_runner *runner = session->request->runner;
    const char *target_root = session->request->target_root;
    bool prepared = layout_partition(runner, &session->options, errors) && layout_format(runner, &session->options, errors) &&
                    layout_mount(runner, &session->options, target_root, errors);
    if (prepared && session->stick.repository != NULL &&
        !offline_ensure_live_keyring(runner, session->request->live_gpg_directory, errors)) {
        runner_say(runner, "offline: the live keyring could not be initialised; pacstrap may refuse the packages");
    }
    return prepared && base_bootstrap(runner, &session->options, &session->stick, target_root, errors) &&
           base_write_fstab(runner, target_root, errors) &&
           base_configure(runner, &session->options, target_root, errors);
}

static void give_data_partition_to_user(install_session *session, error_list *errors) {
    const install_options *options = &session->options;
    if (options->data_label == NULL) {
        return;
    }
    char *owner = text_format("%s:%s", options->username, options->username);
    const char *const give[] = { "chown", owner, options->data_mount, NULL };
    if (!target_run(session->request->runner, session->request->target_root, give, errors)) {
        runner_say(session->request->runner, "data: %s could not be handed to %s", options->data_mount, options->username);
    }
    free(owner);
}

static void install_software(install_session *session, error_list *errors) {
    command_runner *runner = session->request->runner;
    const char *target_root = session->request->target_root;
    offline_attach(runner, &session->stick, target_root, errors);
    software_request software = { .options = &session->options, .resolved = &session->resolved,
                                  .stick = &session->stick, .data_directory = session->request->data_directory,
                                  .target_root = target_root };
    software_install(runner, &software, &session->problems, errors);
    layout_setup_snapshots(runner, &session->options, target_root, errors);
    offline_detach(runner, &session->stick, target_root, errors);
    layout_snapshot_fresh_install(runner, &session->options, target_root, errors);
}

static void install_firstboot_hooks(install_session *session, error_list *errors) {
    hook_list hooks;
    hook_list_init(&hooks);
    if (hooks_read(session->resolved.setup, &hooks, errors)) {
        firstboot_request firstboot = { .target_root = session->request->target_root,
                                        .setup_name = session->resolved.name, .setup_json = session->setup_json,
                                        .hooks = &hooks, .data_directory = session->request->data_directory,
                                        .personal_directory = session->stick.personal };
        bool installed = false;
        bool succeeded = firstboot_install(&firstboot, &installed, errors);
        firstboot_report(session->resolved.name, session->request->target_root, installed, succeeded,
                         session->request->runner->log);
        if (!succeeded) {
            string_list_add(&session->problems, "the firstboot hooks could not be installed");
        }
    }
    hook_list_free(&hooks);
}

static int finish(install_session *session, error_list *errors) {
    command_runner *runner = session->request->runner;
    const char *const unmount[] = { "umount", "-R", session->request->target_root, NULL };
    runner_run(runner, unmount, errors);
    for (size_t index = 0; index < session->problems.count; index++) {
        runner_say(runner, "PROBLEM: %s", session->problems.items[index]);
    }
    if (session->problems.count > 0) {
        runner_say(runner, "install complete with %zu problem(s) listed above: remove the stick and reboot",
                   session->problems.count);
        return IMPRINT_EXIT_INVALID;
    }
    runner_say(runner, "install complete: remove the stick and reboot");
    return IMPRINT_EXIT_OK;
}

static int write_disk(install_session *session, error_list *errors) {
    print_plan(session);
    if (!prepare_disk(session, errors)) {
        runner_say(session->request->runner, "the install stopped: the disk does not boot yet; boot the stick again");
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    give_data_partition_to_user(session, errors);
    install_software(session, errors);
    if (!bootloader_install(session->request->runner, &session->options, session->request->target_root, errors)) {
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    install_firstboot_hooks(session, errors);
    return finish(session, errors);
}

int install_run(const install_request *request, error_list *errors) {
    install_session session;
    session_init(&session, request);
    int exit_code = IMPRINT_EXIT_INVALID;
    if (load_setup(&session, errors)) {
        find_stick(&session, errors);
        exit_code = IMPRINT_EXIT_ENVIRONMENT;
        if (disks_read(request->runner, &session.disks, errors)) {
            choose_default_disks(&session);
            exit_code = IMPRINT_EXIT_USAGE;
            if (!install_review(request->screen, &session.options, &session.disks, session.stick_disk)) {
                error_push(errors, NULL, 0, "the install was cancelled: nothing was changed");
            } else if (confirm_target(&session, errors)) {
                exit_code = write_disk(&session, errors);
            }
        }
    }
    session_free(&session);
    return exit_code;
}
