#include "offline.h"

#include "document.h"
#include "file_tree.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char FLATPAK_EXPORT_INSIDE[] = "/var/lib/imprint-flatpak";

static const char STAND_IN_LIST[] = ".imprint-stand-ins";
static const char PARKED_SUFFIX[] = ".imprint-offline";

/**
 * @implNote A network link without internet stalls every pacman call for hours on mirror DNS lookups; a missing file://
 * server rather than none, since a repository without any server aborts the whole sync, the stick's included.
 */
static const char PARKED_MIRRORLIST[] = "Server = file:///var/lib/imprint-no-mirror/$repo/os/$arch\n";

static char *found_path(const char *stick_root, const char *folder, const char *inside, bool want_directory) {
    char *base = path_join(stick_root, folder);
    char *candidate = path_join(base, inside);
    free(base);
    bool exists = want_directory ? platform_is_directory(candidate) : platform_is_file(candidate);
    if (!exists) {
        free(candidate);
        return NULL;
    }
    return candidate;
}

static char *first_found(const char *stick_root, const char *inside, bool want_directory) {
    char *found = found_path(stick_root, "imprint", inside, want_directory);
    return found != NULL ? found : found_path(stick_root, "ventoy-deploy", inside, want_directory);
}

static char *parent_of(char *path, int levels) {
    for (int level = 0; path != NULL && level < levels; level++) {
        char *slash = strrchr(path, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
    }
    return path;
}

void offline_stick_find(const char *stick_root, offline_stick *stick) {
    memset(stick, 0, sizeof *stick);
    if (stick_root == NULL) {
        return;
    }
    stick->repository = parent_of(first_found(stick_root, "offline/offline.db", false), 1);
    stick->aur_repository = parent_of(first_found(stick_root, "offline-aur/offline-aur.db", false), 1);
    stick->flatpak_export = parent_of(first_found(stick_root, "offline-flatpak/.ostree/repo", true), 2);
    stick->personal = first_found(stick_root, "personal", true);
}

void offline_stick_free(offline_stick *stick) {
    free(stick->repository);
    free(stick->aur_repository);
    free(stick->flatpak_export);
    free(stick->personal);
    memset(stick, 0, sizeof *stick);
}

static const char *find_core_section(const char *conf) {
    if (strncmp(conf, "[core]", 6) == 0) {
        return conf;
    }
    const char *found = strstr(conf, "\n[core]");
    return found == NULL ? NULL : found + 1;
}

char *offline_pacman_conf_text(const char *host_conf_text, const char *repository) {
    const char *core = find_core_section(host_conf_text);
    size_t kept = core == NULL ? strlen(host_conf_text) : (size_t)(core - host_conf_text);
    return text_format("%.*s\n[offline]\nServer = file://%s\n", (int)kept, host_conf_text, repository);
}

char *pacman_conf_insert_before_core(const char *conf, const char *section) {
    const char *core = find_core_section(conf);
    if (core == NULL) {
        return text_format("%s\n%s", conf, section);
    }
    return text_format("%.*s%s%s", (int)(core - conf), conf, section, core);
}

char *pacman_conf_remove_section(const char *conf, const char *name) {
    char *header = text_format("[%s]\n", name);
    const char *start = strncmp(conf, header, strlen(header)) == 0 ? conf : NULL;
    if (start == NULL) {
        char *with_newline = text_format("\n%s", header);
        const char *found = strstr(conf, with_newline);
        start = found == NULL ? NULL : found + 1;
        free(with_newline);
    }
    free(header);
    if (start == NULL) {
        return text_duplicate(conf);
    }
    const char *blank = strstr(start, "\n\n");
    const char *end = blank == NULL ? start + strlen(start) : blank + 2;
    return text_format("%.*s%s", (int)(start - conf), conf, end);
}

bool offline_ensure_live_keyring(command_runner *runner, const char *gpg_directory, error_list *errors) {
    char *public_ring = path_join(gpg_directory, "pubring.gpg");
    bool present = platform_is_file(public_ring);
    free(public_ring);
    if (present) {
        return true;
    }
    runner_say(runner, "offline: initialising the live keyring, pacman-init waits for a network time sync");
    const char *const initialise[] = { "pacman-key", "--gpgdir", gpg_directory, "--init", NULL };
    const char *const populate[] = { "pacman-key", "--gpgdir", gpg_directory, "--populate", "archlinux", NULL };
    return runner_succeeds(runner, initialise, errors) && runner_succeeds(runner, populate, errors);
}

static bool edit_file(const char *path, char *(*transform)(const char *text, const char *argument),
                      const char *argument, error_list *errors) {
    char *text = NULL;
    if (!file_read_all(path, &text, errors)) {
        return false;
    }
    char *changed = transform(text, argument);
    bool written = file_write_text(path, changed, errors);
    free(text);
    free(changed);
    return written;
}

static char *bind_point(const char *target_root, const char *name) {
    return text_format("%s/var/lib/imprint-%s", target_root, name);
}

static bool bind_mount(command_runner *runner, const char *source, const char *target_root, const char *name,
                       error_list *errors) {
    char *inside = bind_point(target_root, name);
    const char *const make[] = { "mkdir", "-p", inside, NULL };
    const char *const bind[] = { "mount", "--bind", source, inside, NULL };
    bool bound = runner_succeeds(runner, make, errors) && runner_succeeds(runner, bind, errors);
    if (!bound) {
        runner_say(runner, "offline: could not attach %s to the new system", name);
    }
    free(inside);
    return bound;
}

static bool attach_repository(command_runner *runner, const char *name, const char *source, const char *signature_level,
                              const char *target_root, error_list *errors) {
    if (!bind_mount(runner, source, target_root, name, errors)) {
        return false;
    }
    char *conf = text_format("%s/etc/pacman.conf", target_root);
    char *section = text_format("[%s]\n%sServer = file:///var/lib/imprint-%s\n\n", name, signature_level, name);
    bool added = edit_file(conf, pacman_conf_insert_before_core, section, errors);
    free(section);
    free(conf);
    return added;
}

static char *parked_mirrorlist(const char *mirrorlist, const char *unused) {
    (void)mirrorlist;
    (void)unused;
    return text_duplicate(PARKED_MIRRORLIST);
}

static bool park_mirrorlist(const char *target_root, error_list *errors) {
    char *list = text_format("%s/etc/pacman.d/mirrorlist", target_root);
    char *parked = text_format("%s%s", list, PARKED_SUFFIX);
    bool done = !platform_is_file(list) || (file_copy(list, parked, errors) &&
                                            edit_file(list, parked_mirrorlist, NULL, errors));
    free(list);
    free(parked);
    return done;
}

bool offline_attach(command_runner *runner, offline_stick *stick, const char *target_root, error_list *errors) {
    if (stick->repository == NULL) {
        runner_say(runner, "offline: no repository on the stick, the configuration uses the mirrors");
        return true;
    }
    stick->repository_attached = attach_repository(runner, "offline", stick->repository, "", target_root, errors) &&
                                 park_mirrorlist(target_root, errors);
    if (stick->repository_attached && stick->aur_repository != NULL) {
        attach_repository(runner, "offline-aur", stick->aur_repository, "SigLevel = Optional TrustAll\n", target_root,
                          errors);
    }
    if (stick->flatpak_export != NULL) {
        stick->flatpak_attached = bind_mount(runner, stick->flatpak_export, target_root, "flatpak", errors);
    }
    return stick->repository_attached;
}

static bool has_database(const char *sync_directory, const char *repository) {
    char *name = text_format("%s.db", repository);
    char *path = path_join(sync_directory, name);
    bool present = platform_is_file(path);
    free(name);
    free(path);
    return present;
}

/** @implNote A stand-in newer than the stick's database looks up to date to pacman, which then never reads the stick. */
static bool is_stick_repository(const char *name) {
    return strcmp(name, "offline") == 0 || strcmp(name, "offline-aur") == 0;
}

static void append_line(const char *path, const char *line, error_list *errors) {
    char *text = NULL;
    if (!platform_is_file(path) || !file_read_all(path, &text, errors)) {
        text = text_duplicate("");
    }
    char *longer = text_format("%s%s\n", text, line);
    file_write_text(path, longer, errors);
    free(text);
    free(longer);
}

void offline_stand_in_missing_databases(command_runner *runner, const char *target_root, error_list *errors) {
    const char *const list[] = { "arch-chroot", target_root, "pacman-conf", "--repo-list", NULL };
    char *repositories = NULL;
    if (runner_capture(runner, list, &repositories, errors) != 0) {
        free(repositories);
        return;
    }
    char *sync_directory = text_format("%s/var/lib/pacman/sync", target_root);
    char *stand_in_list = path_join(sync_directory, STAND_IN_LIST);
    directory_make_all(sync_directory, errors);
    for (char *name = strtok(repositories, "\r\n"); name != NULL; name = strtok(NULL, "\r\n")) {
        if (is_stick_repository(name) || has_database(sync_directory, name)) {
            continue;
        }
        char *database = text_format("%s/%s.db", sync_directory, name);
        const char *const empty[] = { "tar", "-czf", database, "-T", "/dev/null", NULL };
        if (runner_run(runner, empty, errors) == 0) {
            append_line(stand_in_list, name, errors);
        }
        free(database);
    }
    free(stand_in_list);
    free(sync_directory);
    free(repositories);
}

static void remove_stand_ins(const char *target_root, error_list *errors) {
    char *sync_directory = text_format("%s/var/lib/pacman/sync", target_root);
    char *stand_in_list = path_join(sync_directory, STAND_IN_LIST);
    char *names = NULL;
    if (platform_is_file(stand_in_list) && file_read_all(stand_in_list, &names, errors)) {
        for (char *name = strtok(names, "\r\n"); name != NULL; name = strtok(NULL, "\r\n")) {
            char *database = text_format("%s/%s.db", sync_directory, name);
            remove(database);
            free(database);
        }
        remove(stand_in_list);
    }
    free(names);
    free(stand_in_list);
    free(sync_directory);
}

static void restore_mirrorlist(const char *target_root, error_list *errors) {
    char *list = text_format("%s/etc/pacman.d/mirrorlist", target_root);
    char *parked = text_format("%s%s", list, PARKED_SUFFIX);
    if (platform_is_file(parked) && file_copy(parked, list, errors)) {
        remove(parked);
    }
    free(list);
    free(parked);
}

static void remove_repository(const char *target_root, const char *name, error_list *errors) {
    char *conf = text_format("%s/etc/pacman.conf", target_root);
    if (platform_is_file(conf)) {
        edit_file(conf, pacman_conf_remove_section, name, errors);
    }
    const char *const suffixes[] = { ".db", ".files", ".db.sig", ".files.sig" };
    for (size_t index = 0; index < COUNT_OF(suffixes); index++) {
        char *database = text_format("%s/var/lib/pacman/sync/%s%s", target_root, name, suffixes[index]);
        remove(database);
        free(database);
    }
    free(conf);
}

static void unbind(command_runner *runner, const char *target_root, const char *name, error_list *errors) {
    char *inside = bind_point(target_root, name);
    const char *const unmount[] = { "umount", inside, NULL };
    const char *const remove_directory[] = { "rmdir", inside, NULL };
    runner_run(runner, unmount, errors);
    runner_run(runner, remove_directory, errors);
    free(inside);
}

void offline_detach(command_runner *runner, const offline_stick *stick, const char *target_root, error_list *errors) {
    if (stick->repository_attached) {
        restore_mirrorlist(target_root, errors);
        remove_stand_ins(target_root, errors);
        remove_repository(target_root, "offline", errors);
        remove_repository(target_root, "offline-aur", errors);
        unbind(runner, target_root, "offline", errors);
        if (stick->aur_repository != NULL) {
            unbind(runner, target_root, "offline-aur", errors);
        }
    }
    if (stick->flatpak_attached) {
        unbind(runner, target_root, "flatpak", errors);
    }
    runner_say(runner, "offline: the stick's repositories are detached from the new system");
}
