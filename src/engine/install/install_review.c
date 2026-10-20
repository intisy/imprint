#include "install.h"

#include <stdlib.h>
#include <string.h>

enum review_field {
    FIELD_START,
    FIELD_DISK,
    FIELD_HOSTNAME,
    FIELD_USERNAME,
    FIELD_PASSWORD,
    FIELD_TIMEZONE,
    FIELD_LOCALE,
    FIELD_ROOT_FILESYSTEM,
    FIELD_SWAP,
    FIELD_DATA_PARTITION,
    FIELD_COUNT
};

static char *disk_line(const install_options *options, const disk_list *disks) {
    const disk *chosen = disks_find(disks, options->disk);
    if (chosen == NULL) {
        return text_duplicate("disk:          none chosen");
    }
    char *description = disk_describe(chosen);
    char *line = text_format("disk:          %s%s", description, disk_holds_anything(chosen) ? " (NOT empty)" : "");
    free(description);
    return line;
}

static char *data_line(const install_options *options, const disk_list *disks) {
    if (options->offered_data_label == NULL) {
        return text_duplicate("data:          none in this setup");
    }
    if (options->data_label == NULL) {
        return text_duplicate("data:          off, root takes the rest of the disk");
    }
    if (options->data_disk != NULL) {
        const disk *chosen = disks_find(disks, options->data_disk);
        char *description = chosen == NULL ? text_duplicate(options->data_disk) : disk_describe(chosen);
        char *line = text_format("data:          all of %s%s, %s labelled %s at %s", description,
                                 chosen != NULL && disk_holds_anything(chosen) ? " (NOT empty)" : "",
                                 options->data_filesystem, options->data_label, options->data_mount);
        free(description);
        return line;
    }
    return text_format("data:          on, rest of the disk, %s labelled %s at %s (root %lld MiB)",
                       options->data_filesystem, options->data_label, options->data_mount, options->root_size_mb);
}

static bool has_password(const install_options *options) {
    return options->password != NULL || options->password_hash != NULL;
}

static void build_items(const install_options *options, const disk_list *disks, char **items) {
    items[FIELD_START] = text_duplicate("START the install");
    items[FIELD_DISK] = disk_line(options, disks);
    items[FIELD_HOSTNAME] = text_format("hostname:      %s", options->hostname);
    items[FIELD_USERNAME] = text_format("user:          %s", options->username);
    items[FIELD_PASSWORD] = text_format("password:      %s", has_password(options) ? "set" : "NOT SET (required)");
    items[FIELD_TIMEZONE] = text_format("time zone:     %s", options->timezone);
    items[FIELD_LOCALE] = text_format("locale:        %s", options->locale);
    items[FIELD_ROOT_FILESYSTEM] = text_format("root fs:       %s%s", root_filesystem_name(options->root_fs),
                                               options->root_fs == ROOT_BTRFS ? ", with snapshots" : "");
    items[FIELD_SWAP] = text_format("swap:          %lld MiB", options->swap_size_mb);
    items[FIELD_DATA_PARTITION] = data_line(options, disks);
}

static void free_items(char **items) {
    for (size_t index = 0; index < FIELD_COUNT; index++) {
        free(items[index]);
    }
}

static void edit_text(ui *screen, const char *prompt, char **field) {
    char *answer = NULL;
    if (ui_text(screen, prompt, *field, &answer) && answer != NULL && answer[0] != '\0') {
        install_options_replace(field, answer);
    }
    free(answer);
}

static void edit_password(ui *screen, install_options *options) {
    char *first = NULL;
    char *second = NULL;
    bool typed = ui_text(screen, "new password for the user", NULL, &first) && first != NULL && first[0] != '\0' &&
                 ui_text(screen, "the same password again", NULL, &second) && second != NULL;
    if (typed && strcmp(first, second) == 0) {
        install_options_replace(&options->password, first);
        install_options_replace(&options->password_hash, NULL);
    } else if (typed) {
        ui_log(screen, UI_WARNING, "the two passwords differ; the password was not changed");
    }
    free(first);
    free(second);
}

static void edit_disk(ui *screen, install_options *options, const disk_list *disks, const char *stick_disk) {
    char **descriptions = memory_allocate((disks->count + 1) * sizeof *descriptions);
    size_t preselected = 0;
    for (size_t index = 0; index < disks->count; index++) {
        descriptions[index] = disk_describe(&disks->items[index]);
        if (options->disk != NULL && strcmp(disks->items[index].path, options->disk) == 0) {
            preselected = index;
        }
    }
    size_t chosen = 0;
    if (disks->count > 0 &&
        ui_choose_one(screen, "the disk to ERASE and install onto", (const char *const *)descriptions, disks->count,
                      preselected, &chosen)) {
        error_list errors;
        error_list_init(&errors);
        if (disk_check_target(disks, disks->items[chosen].path, stick_disk, &errors)) {
            install_options_replace(&options->disk, disks->items[chosen].path);
        } else {
            ui_log(screen, UI_ERROR, errors.items[0].message);
        }
        error_list_free(&errors);
    }
    for (size_t index = 0; index < disks->count; index++) {
        free(descriptions[index]);
    }
    free(descriptions);
}

static void edit_data(ui *screen, install_options *options, const disk_list *disks, const char *stick_disk) {
    if (options->offered_data_label == NULL) {
        ui_log(screen, UI_INFORMATION, "this setup has no data partition");
        return;
    }
    size_t count = disks->count + 2;
    char **choices = memory_allocate(count * sizeof *choices);
    choices[0] = text_duplicate("off");
    choices[1] = text_duplicate("on the system disk, after root");
    for (size_t index = 0; index < disks->count; index++) {
        char *description = disk_describe(&disks->items[index]);
        choices[index + 2] = text_format("all of %s", description);
        free(description);
    }
    size_t chosen = 0;
    if (ui_choose_one(screen, "where the data partition goes", (const char *const *)choices, count, 1, &chosen)) {
        if (chosen < 2) {
            install_options_set_data_partition(options, chosen == 1);
        } else {
            install_options_use_data_disk(options, disks->items[chosen - 2].path);
            error_list errors;
            error_list_init(&errors);
            if (!install_check_data_disk(options, disks, stick_disk, &errors)) {
                ui_log(screen, UI_ERROR, errors.items[0].message);
                install_options_set_data_partition(options, true);
            }
            error_list_free(&errors);
        }
    }
    for (size_t index = 0; index < count; index++) {
        free(choices[index]);
    }
    free(choices);
}

bool install_check_data_disk(const install_options *options, const disk_list *disks, const char *stick_disk,
                             error_list *errors) {
    if (options->data_disk == NULL) {
        return true;
    }
    if (options->disk != NULL && strcmp(options->disk, options->data_disk) == 0) {
        error_push(errors, NULL, 0, "%s is the system disk; choose another disk for the data, or keep it on the system "
                   "disk", options->data_disk);
        return false;
    }
    return disk_check_target(disks, options->data_disk, stick_disk, errors);
}

static void edit_number(ui *screen, const char *prompt, long long *field) {
    char *current = text_format("%lld", *field);
    char *answer = NULL;
    if (ui_text(screen, prompt, current, &answer) && answer != NULL) {
        char *end = NULL;
        long long value = strtoll(answer, &end, 10);
        if (end != answer && *end == '\0' && value > 0) {
            *field = value;
        } else {
            ui_log(screen, UI_WARNING, "a whole number of MiB above 0, please");
        }
    }
    free(current);
    free(answer);
}

static void edit_root_filesystem(ui *screen, install_options *options) {
    const char *const choices[] = { "ext4", "btrfs, with snapshots (snapper)" };
    size_t chosen = 0;
    if (ui_choose_one(screen, "root filesystem", choices, COUNT_OF(choices), options->root_fs == ROOT_BTRFS ? 1 : 0,
                      &chosen)) {
        options->root_fs = chosen == 1 ? ROOT_BTRFS : ROOT_EXT4;
    }
}

static bool ready_to_start(ui *screen, const install_options *options, const disk_list *disks, const char *stick_disk) {
    if (!has_password(options)) {
        ui_log(screen, UI_WARNING, "set the user's password first (it is never defaulted)");
        return false;
    }
    error_list errors;
    error_list_init(&errors);
    bool usable = options->disk != NULL && disk_check_target(disks, options->disk, stick_disk, &errors) &&
                  install_check_data_disk(options, disks, stick_disk, &errors);
    if (!usable) {
        ui_log(screen, UI_ERROR, errors.count > 0 ? errors.items[0].message : "choose the disk to install onto first");
    }
    error_list_free(&errors);
    return usable;
}

static void edit_field(ui *screen, size_t field, install_options *options, const disk_list *disks,
                       const char *stick_disk) {
    switch (field) {
    case FIELD_DISK: edit_disk(screen, options, disks, stick_disk); break;
    case FIELD_HOSTNAME: edit_text(screen, "hostname", &options->hostname); break;
    case FIELD_USERNAME: edit_text(screen, "user name", &options->username); break;
    case FIELD_PASSWORD: edit_password(screen, options); break;
    case FIELD_TIMEZONE: edit_text(screen, "time zone (Region/City)", &options->timezone); break;
    case FIELD_LOCALE: edit_text(screen, "locale", &options->locale); break;
    case FIELD_ROOT_FILESYSTEM: edit_root_filesystem(screen, options); break;
    case FIELD_SWAP: edit_number(screen, "swap in MiB", &options->swap_size_mb); break;
    case FIELD_DATA_PARTITION: edit_data(screen, options, disks, stick_disk); break;
    default: break;
    }
}

bool install_review(ui *screen, install_options *options, const disk_list *disks, const char *stick_disk) {
    for (;;) {
        char *items[FIELD_COUNT];
        build_items(options, disks, items);
        size_t chosen = 0;
        bool picked = ui_menu(screen, "imprint install: review, then START", (const char *const *)items, FIELD_COUNT,
                              &chosen);
        free_items(items);
        if (!picked) {
            return false;
        }
        if (chosen == FIELD_START && ready_to_start(screen, options, disks, stick_disk)) {
            return true;
        }
        edit_field(screen, chosen, options, disks, stick_disk);
    }
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

bool install_confirm_erase(ui *screen, const disk *target, const char *data_label) {
    if (!disk_holds_anything(target)) {
        return true;
    }
    char *description = disk_describe(target);
    char *warning = text_format("%s is NOT empty: %zu partition(s)%s. Installing ERASES everything on it.", description,
                                target->partition_count,
                                disk_holds_label(target, data_label == NULL ? "data" : data_label)
                                    ? ", one of them labelled as a data partition" : "");
    ui_log(screen, UI_WARNING, warning);
    char *prompt = text_format("type '%s' to ERASE it and install, anything else aborts", base_name(target->path));
    char *typed = NULL;
    bool confirmed = ui_text(screen, prompt, NULL, &typed) && typed != NULL &&
                     strcmp(typed, base_name(target->path)) == 0;
    free(typed);
    free(prompt);
    free(warning);
    free(description);
    return confirmed;
}
