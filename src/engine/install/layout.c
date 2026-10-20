#include "layout.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct btrfs_subvolume {
    const char *name;
    const char *mount_point;
} btrfs_subvolume;

/** A rollback of @ leaves home, logs, the package cache and the snapshots themselves alone. */
static const btrfs_subvolume BTRFS_SUBVOLUMES[] = {
    { "@", "" }, { "@home", "/home" }, { "@snapshots", "/.snapshots" }, { "@var_log", "/var/log" },
    { "@pkg", "/var/cache/pacman/pkg" },
};
static const char BTRFS_MOUNT_OPTIONS[] = "noatime,compress=zstd";

char *partition_path(const char *disk, int number) {
    size_t length = strlen(disk);
    bool ends_in_digit = length > 0 && isdigit((unsigned char)disk[length - 1]);
    return text_format(ends_in_digit ? "%sp%d" : "%s%d", disk, number);
}

char *layout_describe(const install_options *options) {
    char *root = options->root_size_mb > 0 ? text_format("%lldM", options->root_size_mb) : text_duplicate("rest");
    char *layout = text_format("ESP %lldM, swap %lldM, root %s (%s)", options->esp_size_mb, options->swap_size_mb, root,
                               root_filesystem_name(options->root_fs));
    if (options->data_label != NULL) {
        char *longer = options->data_disk != NULL
                           ? text_format("%s; %s on all of %s at %s", layout, options->data_label, options->data_disk,
                                         options->data_mount)
                           : text_format("%s, %s rest at %s", layout, options->data_label, options->data_mount);
        free(layout);
        layout = longer;
    }
    free(root);
    return layout;
}

static bool run_or_fail(command_runner *runner, const char *const *arguments, const char *what, error_list *errors) {
    int status = runner_run(runner, arguments, errors);
    if (status == 0) {
        return true;
    }
    if (status > 0) {
        error_push(errors, NULL, 0, "%s failed (%s exited with %d)", what, arguments[0], status);
    }
    return false;
}

char *layout_data_partition(const install_options *options) {
    return options->data_disk != NULL ? partition_path(options->data_disk, 1)
                                      : partition_path(options->disk, LAYOUT_DATA);
}

static bool has_data_on_system_disk(const install_options *options) {
    return options->data_label != NULL && options->data_disk == NULL;
}

static bool partition_data_disk(command_runner *runner, const install_options *options, error_list *errors) {
    char *name = text_format("1:%s", options->data_label);
    const char *const wipe[] = { "wipefs", "-a", options->data_disk, NULL };
    const char *const zap[] = { "sgdisk", "-Z", options->data_disk, NULL };
    const char *const create[] = { "sgdisk", "-n", "1:0:0", "-t", "1:8300", "-c", name, options->data_disk, NULL };
    const char *const probe[] = { "partprobe", options->data_disk, NULL };
    bool partitioned = run_or_fail(runner, wipe, "wiping the data disk", errors) &&
                       run_or_fail(runner, zap, "clearing the data disk's partition table", errors) &&
                       run_or_fail(runner, create, "partitioning the data disk", errors);
    if (partitioned) {
        runner_run(runner, probe, errors);
    }
    free(name);
    return partitioned;
}

bool layout_partition(command_runner *runner, const install_options *options, error_list *errors) {
    char *layout = layout_describe(options);
    runner_say(runner, "partitioning %s (GPT: %s)", options->disk, layout);
    free(layout);
    char *esp = text_format("1:0:+%lldM", options->esp_size_mb);
    char *swap = text_format("2:0:+%lldM", options->swap_size_mb);
    char *root = options->root_size_mb > 0 ? text_format("3:0:+%lldM", options->root_size_mb) : text_duplicate("3:0:0");
    char *data_name = text_format("4:%s", options->data_label == NULL ? "" : options->data_label);
    const char *const wipe[] = { "wipefs", "-a", options->disk, NULL };
    const char *const zap[] = { "sgdisk", "-Z", options->disk, NULL };
    const char *const with_data[] = { "sgdisk", "-n", esp, "-t", "1:ef00", "-c", "1:ESP", "-n", swap, "-t", "2:8200",
                                      "-c", "2:swap", "-n", root, "-t", "3:8300", "-c", "3:root", "-n", "4:0:0", "-t",
                                      "4:8300", "-c", data_name, options->disk, NULL };
    const char *const without_data[] = { "sgdisk", "-n", esp, "-t", "1:ef00", "-c", "1:ESP", "-n", swap, "-t",
                                         "2:8200", "-c", "2:swap", "-n", root, "-t", "3:8300", "-c", "3:root",
                                         options->disk, NULL };
    bool partitioned = run_or_fail(runner, wipe, "wiping the disk", errors) &&
                       run_or_fail(runner, zap, "clearing the partition table", errors) &&
                       run_or_fail(runner, has_data_on_system_disk(options) ? with_data : without_data, "partitioning",
                                   errors) &&
                       (options->data_disk == NULL || partition_data_disk(runner, options, errors));
    if (partitioned) {
        const char *const probe[] = { "partprobe", options->disk, NULL };
        const char *const settle[] = { "udevadm", "settle", NULL };
        runner_run(runner, probe, errors);
        runner_run(runner, settle, errors);
    }
    free(esp);
    free(swap);
    free(root);
    free(data_name);
    return partitioned;
}

static const char *force_flag(const char *filesystem) {
    return strcmp(filesystem, "xfs") == 0 || strcmp(filesystem, "btrfs") == 0 ? "-f" : "-F";
}

static bool format_partition(command_runner *runner, const char *filesystem, const char *label, const char *partition,
                             error_list *errors) {
    char *program = text_format("mkfs.%s", filesystem);
    const char *const with_label[] = { program, force_flag(filesystem), "-L", label, partition, NULL };
    const char *const without_label[] = { program, force_flag(filesystem), partition, NULL };
    bool formatted = run_or_fail(runner, label == NULL ? without_label : with_label, "formatting", errors);
    free(program);
    return formatted;
}

bool layout_format(command_runner *runner, const install_options *options, error_list *errors) {
    runner_say(runner, "formatting the partitions");
    char *esp = partition_path(options->disk, LAYOUT_ESP);
    char *swap = partition_path(options->disk, LAYOUT_SWAP);
    char *root = partition_path(options->disk, LAYOUT_ROOT);
    char *data = layout_data_partition(options);
    const char *const fat[] = { "mkfs.fat", "-F", "32", "-n", "ESP", esp, NULL };
    const char *const make_swap[] = { "mkswap", swap, NULL };
    bool formatted = run_or_fail(runner, fat, "formatting the ESP", errors) &&
                     run_or_fail(runner, make_swap, "making swap", errors) &&
                     format_partition(runner, root_filesystem_name(options->root_fs), "root", root, errors) &&
                     (options->data_label == NULL ||
                      format_partition(runner, options->data_filesystem, options->data_label, data, errors));
    free(esp);
    free(swap);
    free(root);
    free(data);
    return formatted;
}

static bool make_directory(command_runner *runner, const char *path, error_list *errors) {
    const char *const make[] = { "mkdir", "-p", path, NULL };
    return run_or_fail(runner, make, "creating a mount point", errors);
}

static bool mount_subvolume(command_runner *runner, const char *partition, const btrfs_subvolume *subvolume,
                            const char *target_root, error_list *errors) {
    char *where = text_format("%s%s", target_root, subvolume->mount_point);
    char *options = text_format("subvol=%s,%s", subvolume->name, BTRFS_MOUNT_OPTIONS);
    const char *const mount[] = { "mount", "-o", options, partition, where, NULL };
    bool mounted = make_directory(runner, where, errors) && run_or_fail(runner, mount, "mounting a subvolume", errors);
    free(where);
    free(options);
    return mounted;
}

static bool create_subvolumes(command_runner *runner, const char *partition, const char *target_root,
                              error_list *errors) {
    const char *const mount[] = { "mount", partition, target_root, NULL };
    const char *const unmount[] = { "umount", target_root, NULL };
    if (!run_or_fail(runner, mount, "mounting the btrfs root", errors)) {
        return false;
    }
    bool created = true;
    for (size_t index = 0; created && index < COUNT_OF(BTRFS_SUBVOLUMES); index++) {
        char *path = path_join(target_root, BTRFS_SUBVOLUMES[index].name);
        const char *const create[] = { "btrfs", "subvolume", "create", path, NULL };
        created = run_or_fail(runner, create, "creating a btrfs subvolume", errors);
        free(path);
    }
    return run_or_fail(runner, unmount, "unmounting the btrfs root", errors) && created;
}

static bool mount_root(command_runner *runner, const install_options *options, const char *partition,
                       const char *target_root, error_list *errors) {
    if (options->root_fs == ROOT_EXT4) {
        const char *const mount[] = { "mount", partition, target_root, NULL };
        return make_directory(runner, target_root, errors) && run_or_fail(runner, mount, "mounting root", errors);
    }
    bool mounted = make_directory(runner, target_root, errors) && create_subvolumes(runner, partition, target_root, errors);
    for (size_t index = 0; mounted && index < COUNT_OF(BTRFS_SUBVOLUMES); index++) {
        mounted = mount_subvolume(runner, partition, &BTRFS_SUBVOLUMES[index], target_root, errors);
    }
    return mounted;
}

static bool mount_at(command_runner *runner, const char *partition, const char *target_root, const char *inside,
                     error_list *errors) {
    char *where = text_format("%s%s", target_root, inside);
    const char *const mount[] = { "mount", partition, where, NULL };
    bool mounted = make_directory(runner, where, errors) && run_or_fail(runner, mount, "mounting a partition", errors);
    free(where);
    return mounted;
}

bool layout_mount(command_runner *runner, const install_options *options, const char *target_root, error_list *errors) {
    char *esp = partition_path(options->disk, LAYOUT_ESP);
    char *swap = partition_path(options->disk, LAYOUT_SWAP);
    char *root = partition_path(options->disk, LAYOUT_ROOT);
    char *data = layout_data_partition(options);
    const char *const swap_on[] = { "swapon", swap, NULL };
    bool mounted = mount_root(runner, options, root, target_root, errors) &&
                   mount_at(runner, esp, target_root, "/boot", errors) &&
                   (options->data_label == NULL || mount_at(runner, data, target_root, options->data_mount, errors));
    if (mounted && runner_run(runner, swap_on, errors) != 0) {
        runner_say(runner, "swap could not be turned on; fstab will lack it");
    }
    free(esp);
    free(swap);
    free(root);
    free(data);
    return mounted;
}

void snapshot_packages(string_list *packages) {
    const char *const names[] = { "snapper", "snap-pac", "grub-btrfs", "inotify-tools" };
    for (size_t index = 0; index < COUNT_OF(names); index++) {
        string_list_add_unique(packages, names[index]);
    }
}

static void limit_snapshots(command_runner *runner, const install_options *options, const char *target_root,
                            error_list *errors) {
    char *config = text_format("%s/etc/snapper/configs/root", target_root);
    char *users = text_format("s/^ALLOW_USERS=.*/ALLOW_USERS=\"%s\"/", options->username);
    const char *const edit[] = { "sed", "-i", "-e", users, "-e", "s/^TIMELINE_LIMIT_HOURLY=.*/TIMELINE_LIMIT_HOURLY=\"5\"/",
                                 "-e", "s/^TIMELINE_LIMIT_DAILY=.*/TIMELINE_LIMIT_DAILY=\"7\"/",
                                 "-e", "s/^TIMELINE_LIMIT_MONTHLY=.*/TIMELINE_LIMIT_MONTHLY=\"0\"/",
                                 "-e", "s/^TIMELINE_LIMIT_YEARLY=.*/TIMELINE_LIMIT_YEARLY=\"0\"/",
                                 "-e", "s/^NUMBER_LIMIT=.*/NUMBER_LIMIT=\"20\"/", config, NULL };
    runner_run(runner, edit, errors);
    free(users);
    free(config);
}

/**
 * @implNote snapper's create-config makes its own .snapshots subvolume inside @; it is swapped for @snapshots so the
 * snapshots survive a rollback of @.
 */
void layout_setup_snapshots(command_runner *runner, const install_options *options, const char *target_root,
                            error_list *errors) {
    if (options->root_fs != ROOT_BTRFS) {
        return;
    }
    const char *const install[] = { "arch-chroot", target_root, "pacman", "-S", "--needed", "--noconfirm", "snapper",
                                    "snap-pac", "grub-btrfs", "inotify-tools", NULL };
    if (runner_run(runner, install, errors) != 0) {
        runner_say(runner, "snapshots: snapper could not be installed, the btrfs root has no automatic snapshots");
        return;
    }
    char *snapshots = text_format("%s/.snapshots", target_root);
    char *root = partition_path(options->disk, LAYOUT_ROOT);
    const char *const unmount[] = { "umount", snapshots, NULL };
    const char *const remove[] = { "rmdir", snapshots, NULL };
    const char *const create_config[] = { "arch-chroot", target_root, "snapper", "--no-dbus", "-c", "root",
                                          "create-config", "/", NULL };
    const char *const delete_own[] = { "btrfs", "subvolume", "delete", snapshots, NULL };
    const char *const restrict_access[] = { "chmod", "750", snapshots, NULL };
    const btrfs_subvolume snapshot_volume = BTRFS_SUBVOLUMES[2];
    bool swapped = runner_run(runner, unmount, errors) == 0 && runner_run(runner, remove, errors) == 0 &&
                   runner_run(runner, create_config, errors) == 0 && runner_run(runner, delete_own, errors) == 0 &&
                   mount_subvolume(runner, root, &snapshot_volume, target_root, errors) &&
                   runner_run(runner, restrict_access, errors) == 0;
    if (swapped) {
        limit_snapshots(runner, options, target_root, errors);
        const char *const enable[] = { "arch-chroot", target_root, "systemctl", "enable", "snapper-timeline.timer",
                                       "snapper-cleanup.timer", "grub-btrfsd.service", NULL };
        runner_run(runner, enable, errors);
        runner_say(runner, "snapshots: snapper on @snapshots with its timers");
    } else {
        runner_say(runner, "snapshots: snapper could not be set up on @snapshots");
    }
    free(snapshots);
    free(root);
}

void layout_snapshot_fresh_install(command_runner *runner, const install_options *options, const char *target_root,
                                   error_list *errors) {
    if (options->root_fs != ROOT_BTRFS) {
        return;
    }
    const char *const snapshot[] = { "arch-chroot", target_root, "snapper", "--no-dbus", "-c", "root", "create",
                                     "--description", "fresh install", NULL };
    if (runner_run(runner, snapshot, errors) != 0) {
        runner_say(runner, "snapshots: the first snapshot could not be taken");
    }
}
