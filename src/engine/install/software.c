#include "software.h"

#include "base_system.h"
#include "document.h"
#include "file_tree.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PACKAGE_ATTEMPTS = 3 };

static const char BUILD_SUDOERS[] = "/etc/sudoers.d/zz-imprint-build";

static bool run_list_in_target(command_runner *runner, const char *target_root, const char *const *prefix,
                               const string_list *items, error_list *errors) {
    size_t prefix_count = 0;
    while (prefix[prefix_count] != NULL) {
        prefix_count++;
    }
    const char **command = memory_allocate((prefix_count + items->count + 1) * sizeof *command);
    memcpy(command, prefix, prefix_count * sizeof *command);
    for (size_t index = 0; index < items->count; index++) {
        command[prefix_count + index] = items->items[index];
    }
    command[prefix_count + items->count] = NULL;
    bool succeeded = target_run(runner, target_root, command, errors);
    free(command);
    return succeeded;
}

static void add_problem(string_list *problems, const char *format, const char *subject) {
    char *problem = text_format(format, subject);
    string_list_add(problems, problem);
    free(problem);
}

static void run_repository_setup(command_runner *runner, const software_request *request, error_list *errors) {
    for (size_t index = 0; index < request->resolved->repo_setup.count; index++) {
        const char *const command[] = { "sh", "-c", request->resolved->repo_setup.items[index], NULL };
        if (!target_run(runner, request->target_root, command, errors)) {
            runner_say(runner, "repository setup failed: %s", request->resolved->repo_setup.items[index]);
        }
    }
}

/** @implNote One stalled download fails the whole transaction; what was fetched stays cached, so a retry costs less. */
static bool install_repository_packages(command_runner *runner, const software_request *request,
                                        const string_list *packages, string_list *problems, error_list *errors) {
    if (packages->count == 0) {
        return true;
    }
    const char *const install[] = { "pacman", "-S", "--noconfirm", "--needed", NULL };
    for (int attempt = 1; attempt <= PACKAGE_ATTEMPTS; attempt++) {
        if (run_list_in_target(runner, request->target_root, install, packages, errors)) {
            return true;
        }
        runner_say(runner, "packages: attempt %d of %d failed", attempt, PACKAGE_ATTEMPTS);
    }
    string_list_add(problems, "the repository packages are not installed; rerun pacman -S --needed with the setup's "
                              "packages");
    return false;
}

char *build_sudoers_path(const char *target_root) {
    return target_path(target_root, BUILD_SUDOERS);
}

/**
 * @implNote AUR helpers call sudo pacman as the user and nobody is there to type a password, so the user gets
 * passwordless sudo for this step only. The file sorts last because sudo applies the last matching rule.
 */
static bool grant_build_sudo(const software_request *request, error_list *errors) {
    char *rule = text_format("%s ALL=(ALL:ALL) NOPASSWD: ALL\n", request->options->username);
    char *path = build_sudoers_path(request->target_root);
    bool granted = target_write(request->target_root, BUILD_SUDOERS, rule, errors) && platform_set_mode(path, 0440, errors);
    free(rule);
    free(path);
    return granted;
}

static void revoke_build_sudo(const software_request *request) {
    char *path = build_sudoers_path(request->target_root);
    remove(path);
    free(path);
}

static bool ensure_aur_helper(command_runner *runner, const software_request *request, error_list *errors) {
    const char *helper = request->options->aur_helper;
    char *check = text_format("command -v %s", helper);
    const char *const present[] = { "sh", "-c", check, NULL };
    bool available = target_run(runner, request->target_root, present, errors);
    free(check);
    if (available) {
        return true;
    }
    char *build = text_format("cd /tmp && rm -rf %s-bin && git clone https://aur.archlinux.org/%s-bin.git && "
                              "cd %s-bin && makepkg -si --noconfirm", helper, helper, helper);
    const char *const make[] = { "sudo", "-u", request->options->username, "-H", "sh", "-c", build, NULL };
    available = target_run(runner, request->target_root, make, errors);
    free(build);
    return available;
}

static void build_from_aur(command_runner *runner, const software_request *request, const string_list *packages,
                           string_list *problems, error_list *errors) {
    if (packages->count == 0) {
        return;
    }
    if (!ensure_aur_helper(runner, request, errors)) {
        for (size_t index = 0; index < packages->count; index++) {
            add_problem(problems, "AUR: %s is not installed (not on the stick, and the AUR helper is unavailable)",
                        packages->items[index]);
        }
        return;
    }
    for (size_t index = 0; index < packages->count; index++) {
        const char *const build[] = { "sudo", "-u", request->options->username, "-H", request->options->aur_helper, "-S",
                                      "--noconfirm", "--needed", packages->items[index], NULL };
        if (!target_run(runner, request->target_root, build, errors)) {
            add_problem(problems, "AUR: %s could not be built", packages->items[index]);
        }
    }
}

/** One install per package, so a single AUR package that fails does not cost every other one. */
static void install_aur_packages(command_runner *runner, const software_request *request, string_list *problems,
                                 error_list *errors) {
    const string_list *aur = &request->resolved->aur;
    if (aur->count == 0) {
        return;
    }
    string_list from_aur;
    string_list_init(&from_aur);
    for (size_t index = 0; index < aur->count; index++) {
        const char *const query[] = { "pacman", "-Si", aur->items[index], NULL };
        const char *const install[] = { "pacman", "-S", "--noconfirm", "--needed", aur->items[index], NULL };
        if (!target_run(runner, request->target_root, query, errors)) {
            string_list_add(&from_aur, aur->items[index]);
        } else if (!target_run(runner, request->target_root, install, errors)) {
            add_problem(problems, "AUR: %s failed to install from the stick's repository", aur->items[index]);
        }
    }
    if (from_aur.count > 0 && grant_build_sudo(request, errors)) {
        build_from_aur(runner, request, &from_aur, problems, errors);
        revoke_build_sudo(request);
    }
    string_list_free(&from_aur);
}

static bool flatpak_available(command_runner *runner, const software_request *request, error_list *errors) {
    const char *const present[] = { "sh", "-c", "command -v flatpak", NULL };
    return target_run(runner, request->target_root, present, errors);
}

/**
 * @implNote Flatpak sideloads only for a remote with a collection ID, which Flathub's definition lacks; without one
 * it insists on fetching the summary from the internet.
 */
static void install_flatpaks(command_runner *runner, const software_request *request, string_list *problems,
                             error_list *errors) {
    const string_list *apps = &request->resolved->flatpak;
    if (apps->count == 0) {
        return;
    }
    if (!flatpak_available(runner, request, errors)) {
        string_list_add(problems, "flatpak: not installed in the new system, so no Flatpak app was installed");
        return;
    }
    bool sideload = request->stick->flatpak_attached;
    char *definition = sideload ? text_format("%s/flathub.flatpakrepo", FLATPAK_EXPORT_INSIDE)
                                : text_duplicate("https://flathub.org/repo/flathub.flatpakrepo");
    char *sideload_option = text_format("--sideload-repo=%s/.ostree/repo", FLATPAK_EXPORT_INSIDE);
    const char *const add[] = { "flatpak", "remote-add", "--if-not-exists", "flathub", definition, NULL };
    const char *const collection[] = { "flatpak", "remote-modify", "--collection-id=org.flathub.Stable", "flathub",
                                       NULL };
    target_run(runner, request->target_root, add, errors);
    if (sideload) {
        target_run(runner, request->target_root, collection, errors);
    }
    for (size_t index = 0; index < apps->count; index++) {
        const char *const with_stick[] = { "flatpak", "install", "-y", "--noninteractive", sideload_option, "flathub",
                                           apps->items[index], NULL };
        const char *const online[] = { "flatpak", "install", "-y", "--noninteractive", "flathub", apps->items[index],
                                       NULL };
        if (!target_run(runner, request->target_root, sideload ? with_stick : online, errors)) {
            add_problem(problems, "flatpak: %s failed to install", apps->items[index]);
        }
    }
    free(definition);
    free(sideload_option);
}

static void enable_services(command_runner *runner, const software_request *request, string_list *problems,
                            error_list *errors) {
    for (size_t index = 0; index < request->resolved->services.count; index++) {
        const char *service = request->resolved->services.items[index];
        const char *const enable[] = { "systemctl", "enable", service, NULL };
        if (!target_run(runner, request->target_root, enable, errors)) {
            add_problem(problems, "service: %s could not be enabled", service);
        }
    }
}

static char *home_path(const software_request *request, const char *inside) {
    return text_format("%s/home/%s/%s", request->target_root, request->options->username, inside);
}

static char *parent_of(const char *path) {
    char *parent = text_duplicate(path);
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    return parent;
}

static void copy_dotfiles(command_runner *runner, const software_request *request, error_list *errors) {
    for (size_t index = 0; index < request->resolved->dotfiles.count; index++) {
        const dotfile *file = &request->resolved->dotfiles.items[index];
        char *source = path_join(request->data_directory, file->source);
        char *target = home_path(request, file->target);
        char *parent = parent_of(target);
        if (!platform_is_file(source)) {
            runner_say(runner, "dotfiles: %s is not in the data repo, skipped", file->source);
        } else if (directory_make_all(parent, errors) && file_copy(source, target, errors)) {
            runner_say(runner, "dotfiles: ~/%s", file->target);
        }
        free(source);
        free(target);
        free(parent);
    }
}

static void report_host_files(command_runner *runner, const software_request *request) {
    const document_node *host_files = document_get(request->resolved->setup, "host_files");
    for (size_t index = 0; host_files != NULL && index < host_files->child_count; index++) {
        const document_node *target = document_get(host_files->children[index], "target");
        runner_say(runner, "host files: %s is not bundled by this imprint version, skipped",
                   target != NULL && target->kind == DOCUMENT_STRING ? target->string_value : "(no target)");
    }
}

/** @implNote No user manager runs in a chroot, so the link systemctl --user enable would make is made by hand. */
static void enable_user_services(command_runner *runner, const software_request *request, error_list *errors) {
    const string_list *units = &request->resolved->user_services;
    if (units->count == 0) {
        return;
    }
    char *wants = home_path(request, ".config/systemd/user/default.target.wants");
    directory_make_all(wants, errors);
    for (size_t index = 0; index < units->count; index++) {
        const char *unit = units->items[index];
        char *own = text_format("/home/%s/.config/systemd/user/%s", request->options->username, unit);
        char *system_wide = text_format("/usr/lib/systemd/user/%s", unit);
        char *own_on_target = target_path(request->target_root, own);
        char *system_on_target = target_path(request->target_root, system_wide);
        const char *source = platform_is_file(own_on_target) ? own : platform_is_file(system_on_target) ? system_wide : NULL;
        char *link = path_join(wants, unit);
        if (source == NULL) {
            runner_say(runner, "user service: %s not found, skipped", unit);
        } else if (platform_make_symlink(source, link, errors)) {
            runner_say(runner, "user service: enabled %s", unit);
        }
        free(link);
        free(own);
        free(system_wide);
        free(own_on_target);
        free(system_on_target);
    }
    free(wants);
}

static void apply_remote_access(command_runner *runner, const software_request *request, error_list *errors) {
    const string_list *keys = &request->options->authorized_keys;
    if (keys->count == 0) {
        return;
    }
    char *directory = home_path(request, ".ssh");
    char *file = home_path(request, ".ssh/authorized_keys");
    char *joined = string_list_join(keys, "\n");
    char *text = text_format("%s\n", joined);
    bool written = directory_make_all(directory, errors) && file_write_text(file, text, errors) &&
                   platform_set_mode(directory, 0700, errors) && platform_set_mode(file, 0600, errors) &&
                   target_write(request->target_root, "/etc/ssh/sshd_config.d/10-imprint.conf",
                                "PasswordAuthentication no\nKbdInteractiveAuthentication no\nPermitRootLogin no\n", errors);
    if (written) {
        runner_say(runner, "remote: %zu key(s) for %s, sshd key-only", keys->count, request->options->username);
    }
    free(directory);
    free(file);
    free(joined);
    free(text);
}

static void write_notes(const software_request *request, error_list *errors) {
    if (request->options->notes.count == 0) {
        return;
    }
    char *joined = string_list_join(&request->options->notes, "\n");
    char *text = text_format("%s\n", joined);
    char *in_home = text_format("/home/%s/IMPRINT-SETUP-NOTES.txt", request->options->username);
    target_write(request->target_root, "/etc/imprint/NOTES.txt", text, errors);
    target_write(request->target_root, in_home, text, errors);
    free(joined);
    free(text);
    free(in_home);
}

void display_manager_packages(const char *display_manager, string_list *packages) {
    if (strcmp(display_manager, "lightdm") == 0) {
        string_list_add(packages, "lightdm");
        string_list_add(packages, "lightdm-gtk-greeter");
    } else {
        string_list_add(packages, display_manager);
    }
}

static void enable_display_manager(command_runner *runner, const software_request *request, string_list *problems,
                                   error_list *errors) {
    const char *display_manager = request->options->display_manager;
    if (!install_options_have_display_manager(request->options)) {
        return;
    }
    string_list packages;
    string_list_init(&packages);
    display_manager_packages(display_manager, &packages);
    const char *const install[] = { "pacman", "-S", "--noconfirm", "--needed", NULL };
    const char *const enable[] = { "systemctl", "enable", display_manager, NULL };
    if (!run_list_in_target(runner, request->target_root, install, &packages, errors) ||
        !target_run(runner, request->target_root, enable, errors)) {
        add_problem(problems, "display manager: %s could not be installed and enabled", display_manager);
    }
    string_list_free(&packages);
}

static void hand_home_to_user(command_runner *runner, const software_request *request, error_list *errors) {
    char *owner = text_format("%s:%s", request->options->username, request->options->username);
    char *home = text_format("/home/%s", request->options->username);
    const char *const give[] = { "chown", "-R", owner, home, NULL };
    target_run(runner, request->target_root, give, errors);
    free(owner);
    free(home);
}

bool software_install(command_runner *runner, const software_request *request, string_list *problems,
                      error_list *errors) {
    runner_say(runner, "installing the setup's software");
    run_repository_setup(runner, request, errors);
    if (request->stick->repository_attached) {
        offline_stand_in_missing_databases(runner, request->target_root, errors);
    }
    const char *const upgrade[] = { "pacman", "-Syu", "--noconfirm", NULL };
    target_run(runner, request->target_root, upgrade, errors);
    bool installed = install_repository_packages(runner, request, &request->resolved->packages, problems, errors);
    install_aur_packages(runner, request, problems, errors);
    install_flatpaks(runner, request, problems, errors);
    enable_services(runner, request, problems, errors);
    enable_display_manager(runner, request, problems, errors);
    base_apply_user_groups(runner, request->options, request->target_root, errors);
    copy_dotfiles(runner, request, errors);
    report_host_files(runner, request);
    enable_user_services(runner, request, errors);
    apply_remote_access(runner, request, errors);
    write_notes(request, errors);
    hand_home_to_user(runner, request, errors);
    return installed;
}
