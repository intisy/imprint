#include "base_system.h"

#include "document.h"
#include "file_tree.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PACSTRAP_MIRROR_ATTEMPTS = 3 };

static const char LIVE_PACMAN_CONF[] = "/etc/pacman.conf";
static const char PASSWORD_FILE[] = "/root/.imprint-password";

char *target_path(const char *target_root, const char *inside) {
    return text_format("%s%s", target_root, inside);
}

static char *parent_directory(const char *path) {
    char *parent = text_duplicate(path);
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    return parent;
}

bool target_write(const char *target_root, const char *inside, const char *text, error_list *errors) {
    char *path = target_path(target_root, inside);
    char *parent = parent_directory(path);
    bool written = directory_make_all(parent, errors) && file_write_text(path, text, errors);
    free(parent);
    free(path);
    return written;
}

bool target_append(const char *target_root, const char *inside, const char *text, error_list *errors) {
    char *path = target_path(target_root, inside);
    char *existing = NULL;
    if (!platform_is_file(path) || !file_read_all(path, &existing, errors)) {
        existing = text_duplicate("");
    }
    char *longer = text_format("%s%s", existing, text);
    free(path);
    bool written = target_write(target_root, inside, longer, errors);
    free(existing);
    free(longer);
    return written;
}

void base_packages(const install_options *options, string_list *packages) {
    const char *const always[] = { "base", "base-devel", "linux", "linux-firmware", "linux-headers", "sudo",
                                   "networkmanager", "vim", "git", "intel-ucode", "amd-ucode" };
    for (size_t index = 0; index < COUNT_OF(always); index++) {
        string_list_add_unique(packages, always[index]);
    }
    if (strcmp(options->bootloader, "grub") == 0) {
        string_list_add_unique(packages, "grub");
    } else if (strcmp(options->bootloader, "refind") == 0) {
        string_list_add_unique(packages, "refind");
    }
    string_list_add_unique(packages, "efibootmgr");
    if (options->root_fs == ROOT_BTRFS) {
        string_list_add_unique(packages, "btrfs-progs");
    }
}

static int run_pacstrap(command_runner *runner, const char *conf, const char *target_root, const string_list *packages,
                        error_list *errors) {
    size_t count = 0;
    const char **arguments = memory_allocate((packages->count + 6) * sizeof *arguments);
    arguments[count++] = "pacstrap";
    if (conf != NULL) {
        arguments[count++] = "-C";
        arguments[count++] = conf;
    }
    arguments[count++] = "-K";
    arguments[count++] = target_root;
    for (size_t index = 0; index < packages->count; index++) {
        arguments[count++] = packages->items[index];
    }
    arguments[count] = NULL;
    int status = runner_run(runner, arguments, errors);
    free(arguments);
    return status;
}

static bool bootstrap_from_stick(command_runner *runner, const offline_stick *stick, const char *target_root,
                                 const string_list *packages, error_list *errors) {
    char *host_conf = NULL;
    if (stick->repository == NULL || !file_read_all(LIVE_PACMAN_CONF, &host_conf, errors)) {
        free(host_conf);
        return false;
    }
    char *conf_text = offline_pacman_conf_text(host_conf, stick->repository);
    char *conf = platform_temporary_path(errors);
    bool installed = conf != NULL && file_write_text(conf, conf_text, errors) &&
                     run_pacstrap(runner, conf, target_root, packages, errors) == 0;
    if (conf != NULL) {
        remove(conf);
    }
    free(conf);
    free(conf_text);
    free(host_conf);
    return installed;
}

bool base_bootstrap(command_runner *runner, const install_options *options, const offline_stick *stick,
                    const char *target_root, error_list *errors) {
    string_list packages;
    string_list_init(&packages);
    base_packages(options, &packages);
    runner_say(runner, "bootstrapping the base system into %s", target_root);
    bool installed = bootstrap_from_stick(runner, stick, target_root, &packages, errors);
    if (!installed && stick->repository != NULL) {
        runner_say(runner, "bootstrap: the stick's repository could not complete the base system, using the mirrors");
    }
    for (int attempt = 1; !installed && attempt <= PACSTRAP_MIRROR_ATTEMPTS; attempt++) {
        installed = run_pacstrap(runner, NULL, target_root, &packages, errors) == 0;
        if (!installed) {
            runner_say(runner, "bootstrap: pacstrap attempt %d of %d failed", attempt, PACSTRAP_MIRROR_ATTEMPTS);
        }
    }
    string_list_free(&packages);
    if (!installed) {
        error_push(errors, NULL, 0, "the base system could not be installed, neither from the stick nor the mirrors");
    }
    return installed;
}

bool base_write_fstab(command_runner *runner, const char *target_root, error_list *errors) {
    const char *const generate[] = { "genfstab", "-U", target_root, NULL };
    char *fstab = NULL;
    if (runner_capture(runner, generate, &fstab, errors) != 0) {
        free(fstab);
        error_push(errors, NULL, 0, "genfstab could not describe the mounted target");
        return false;
    }
    bool written = target_append(target_root, "/etc/fstab", fstab, errors);
    free(fstab);
    return written;
}

bool target_run(command_runner *runner, const char *target_root, const char *const *command, error_list *errors) {
    size_t count = 0;
    while (command[count] != NULL) {
        count++;
    }
    const char **arguments = memory_allocate((count + 3) * sizeof *arguments);
    arguments[0] = "arch-chroot";
    arguments[1] = target_root;
    memcpy(arguments + 2, command, (count + 1) * sizeof *arguments);
    bool succeeded = runner_run(runner, arguments, errors) == 0;
    free(arguments);
    return succeeded;
}

static bool configure_clock_and_locale(command_runner *runner, const install_options *options, const char *target_root,
                                       error_list *errors) {
    char *zone = text_format("/usr/share/zoneinfo/%s", options->timezone);
    const char *const link_zone[] = { "ln", "-sf", zone, "/etc/localtime", NULL };
    const char *const clock[] = { "hwclock", "--systohc", NULL };
    const char *const generate[] = { "locale-gen", NULL };
    char *locale_line = text_format("%s UTF-8\n", options->locale);
    char *language = text_format("LANG=%s\n", options->locale);
    bool configured = target_run(runner, target_root, link_zone, errors);
    if (configured) {
        target_run(runner, target_root, clock, errors);
    }
    configured = configured && target_append(target_root, "/etc/locale.gen", locale_line, errors) &&
                 target_run(runner, target_root, generate, errors) &&
                 target_write(target_root, "/etc/locale.conf", language, errors);
    if (!configured) {
        error_push(errors, NULL, 0, "the time zone '%s' or the locale '%s' could not be set", options->timezone,
                   options->locale);
    }
    free(zone);
    free(locale_line);
    free(language);
    return configured;
}

static bool configure_keyboard(const install_options *options, const char *target_root, error_list *errors) {
    if (options->keyboard_layout == NULL) {
        return true;
    }
    char *console = text_format("KEYMAP=%s\n", options->keyboard_layout);
    char *x11 = text_format("Section \"InputClass\"\n    Identifier \"system-keyboard\"\n    MatchIsKeyboard \"on\"\n"
                            "    Option \"XkbLayout\" \"%s\"\nEndSection\n", options->keyboard_layout);
    char *wayland = text_format("XKB_DEFAULT_LAYOUT=%s\n", options->keyboard_layout);
    bool written = target_write(target_root, "/etc/vconsole.conf", console, errors) &&
                   target_write(target_root, "/etc/X11/xorg.conf.d/00-keyboard.conf", x11, errors) &&
                   target_append(target_root, "/etc/environment", wayland, errors);
    free(console);
    free(x11);
    free(wayland);
    return written;
}

bool base_set_password(command_runner *runner, const char *target_root, const char *name, const char *secret,
                       bool is_hash, error_list *errors) {
    char *line = text_format("%s:%s\n", name, secret);
    char *file = target_path(target_root, PASSWORD_FILE);
    char *feed = text_format("chpasswd %s< %s", is_hash ? "-e " : "", PASSWORD_FILE);
    const char *const set[] = { "sh", "-c", feed, NULL };
    bool written = target_write(target_root, PASSWORD_FILE, "", errors) && platform_set_mode(file, 0600, errors) &&
                   file_write_text(file, line, errors);
    bool set_password = written && target_run(runner, target_root, set, errors);
    remove(file);
    memset(line, 0, strlen(line));
    free(line);
    free(file);
    free(feed);
    if (!set_password) {
        error_push(errors, NULL, 0, "the password of '%s' could not be set", name);
    }
    return set_password;
}

static bool create_primary_user(command_runner *runner, const install_options *options, const char *target_root,
                                error_list *errors) {
    const char *const add[] = { "useradd", "-m", "-s", "/bin/bash", options->username, NULL };
    const char *const exists[] = { "id", "-u", options->username, NULL };
    const char *const wheel[] = { "usermod", "-aG", "wheel", options->username, NULL };
    bool created = target_run(runner, target_root, exists, errors) || target_run(runner, target_root, add, errors);
    created = created && target_run(runner, target_root, wheel, errors) &&
              target_write(target_root, "/etc/sudoers.d/wheel", "%wheel ALL=(ALL:ALL) ALL\n", errors);
    if (!created) {
        error_push(errors, NULL, 0, "the user '%s' could not be created", options->username);
        return false;
    }
    bool has_hash = options->password_hash != NULL;
    return base_set_password(runner, target_root, options->username, has_hash ? options->password_hash : options->password,
                             has_hash, errors);
}

static void apply_password_expiry(command_runner *runner, const install_options *options, const char *target_root,
                                  error_list *errors) {
    if (install_options_should_expire_password(options)) {
        const char *const expire[] = { "chage", "-d", "0", options->username, NULL };
        if (target_run(runner, target_root, expire, errors)) {
            runner_say(runner, "the password of '%s' must be changed at the first login", options->username);
        }
    } else if (options->expire_password) {
        runner_say(runner, "the password of '%s' is not expired: the display manager %s cannot run the change and would "
                   "refuse the login", options->username, options->display_manager);
    }
}

static void apply_setup_user(command_runner *runner, const setup_user *user, const char *target_root,
                             error_list *errors) {
    const char *const exists[] = { "id", "-u", user->name, NULL };
    const char *const add[] = { "useradd", "-m", user->name, NULL };
    if (!target_run(runner, target_root, exists, errors) && !target_run(runner, target_root, add, errors)) {
        runner_say(runner, "user: %s could not be created", user->name);
        return;
    }
    if (user->shell != NULL) {
        const char *const shell[] = { "usermod", "-s", user->shell, user->name, NULL };
        target_run(runner, target_root, shell, errors);
    }
    if (user->sudo) {
        const char *const wheel[] = { "usermod", "-aG", "wheel", user->name, NULL };
        target_run(runner, target_root, wheel, errors);
    }
    if (user->password_hash != NULL) {
        base_set_password(runner, target_root, user->name, user->password_hash, true, errors);
    }
}

bool base_configure(command_runner *runner, const install_options *options, const char *target_root,
                    error_list *errors) {
    runner_say(runner, "configuring the base system (hostname %s, time zone %s, user %s)", options->hostname,
               options->timezone, options->username);
    char *hostname = text_format("%s\n", options->hostname);
    bool configured = target_write(target_root, "/etc/hostname", hostname, errors) &&
                      configure_clock_and_locale(runner, options, target_root, errors) &&
                      configure_keyboard(options, target_root, errors) &&
                      create_primary_user(runner, options, target_root, errors);
    free(hostname);
    if (!configured) {
        return false;
    }
    apply_password_expiry(runner, options, target_root, errors);
    for (size_t index = 0; index < options->user_count; index++) {
        apply_setup_user(runner, &options->users[index], target_root, errors);
    }
    const char *const network[] = { "systemctl", "enable", "NetworkManager", NULL };
    const char *const lock_root[] = { "passwd", "-l", "root", NULL };
    if (!target_run(runner, target_root, network, errors)) {
        runner_say(runner, "NetworkManager could not be enabled");
    }
    target_run(runner, target_root, lock_root, errors);
    return true;
}

void base_apply_user_groups(command_runner *runner, const install_options *options, const char *target_root,
                            error_list *errors) {
    for (size_t index = 0; index < options->user_groups.count; index++) {
        const char *group = options->user_groups.items[index];
        const char *const exists[] = { "getent", "group", group, NULL };
        const char *const join[] = { "usermod", "-aG", group, options->username, NULL };
        if (!target_run(runner, target_root, exists, errors)) {
            runner_say(runner, "group: %s does not exist, skipped", group);
        } else if (!target_run(runner, target_root, join, errors)) {
            runner_say(runner, "group: %s could not be joined", group);
        }
    }
}
