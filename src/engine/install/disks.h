#ifndef IMPRINT_DISKS_H
#define IMPRINT_DISKS_H

#include "command_runner.h"
#include "string_list.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct disk {
    char *path;
    char *name;
    unsigned long long size;
    bool removable;
    bool read_only;
    char *transport;
    char *serial;
    char *model;
    size_t partition_count;
    bool has_filesystem;
    bool mounted;
    string_list labels;
} disk;

typedef struct disk_list {
    disk *items;
    size_t count;
    size_t capacity;
} disk_list;

extern const char *const LSBLK_ARGUMENTS[];

void disk_list_init(disk_list *disks);
void disk_list_free(disk_list *disks);

/** Reads `lsblk -J -b` output; only whole disks are kept, their partitions folded into each disk's fields. */
bool disks_parse_lsblk(const char *json, disk_list *disks, error_list *errors);
bool disks_read(command_runner *runner, disk_list *disks, error_list *errors);
const disk *disks_find(const disk_list *disks, const char *path);

/** The path of the disk under the file system holding the directory, through device-mapper layers; NULL if none. */
char *disks_holding_directory(command_runner *runner, const char *directory, error_list *errors);

/** The largest writable internal disk that is not the stick, or NULL when there is none. */
const disk *disks_default_target(const disk_list *disks, const char *stick_disk);

bool disk_holds_anything(const disk *candidate);
bool disk_holds_label(const disk *candidate, const char *label);

/** Refuses the stick, a removable, read-only or mounted disk, and a path that is not a listed disk. */
bool disk_check_target(const disk_list *disks, const char *path, const char *stick_disk, error_list *errors);
bool disk_same_identity(const disk *before, const disk *now);
char *disk_describe(const disk *candidate);
char *size_describe(unsigned long long bytes);

#endif
