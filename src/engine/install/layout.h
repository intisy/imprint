#ifndef IMPRINT_LAYOUT_H
#define IMPRINT_LAYOUT_H

#include "command_runner.h"
#include "install_options.h"

enum layout_partition_number {
    LAYOUT_ESP = 1,
    LAYOUT_SWAP = 2,
    LAYOUT_ROOT = 3,
    LAYOUT_DATA = 4
};

/** /dev/sda gives /dev/sda1; a disk whose name ends in a digit (nvme0n1, loop0, mmcblk0) gives /dev/nvme0n1p1. */
char *partition_path(const char *disk, int number);
char *layout_describe(const install_options *options);

bool layout_partition(command_runner *runner, const install_options *options, error_list *errors);
bool layout_format(command_runner *runner, const install_options *options, error_list *errors);

/** Mounts root (a btrfs root on its subvolumes), the ESP at /boot and the data partition, and turns swap on. */
bool layout_mount(command_runner *runner, const install_options *options, const char *target_root, error_list *errors);

/** snapper on @snapshots with its timers, for a btrfs root; a failure leaves the root without automatic snapshots. */
void layout_setup_snapshots(command_runner *runner, const install_options *options, const char *target_root,
                            error_list *errors);
void layout_snapshot_fresh_install(command_runner *runner, const install_options *options, const char *target_root,
                                   error_list *errors);
void snapshot_packages(string_list *packages);

#endif
