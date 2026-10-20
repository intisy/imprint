#include "disks.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

const char *const LSBLK_ARGUMENTS[] = {
    "lsblk", "-J", "-b", "-o", "NAME,PATH,SIZE,TYPE,RM,RO,TRAN,SERIAL,MODEL,FSTYPE,LABEL,PARTLABEL,MOUNTPOINTS", NULL
};

void disk_list_init(disk_list *disks) {
    memset(disks, 0, sizeof *disks);
}

static void disk_free(disk *item) {
    free(item->path);
    free(item->name);
    free(item->transport);
    free(item->serial);
    free(item->model);
    string_list_free(&item->labels);
}

void disk_list_free(disk_list *disks) {
    for (size_t index = 0; index < disks->count; index++) {
        disk_free(&disks->items[index]);
    }
    free(disks->items);
    disk_list_init(disks);
}

static disk *disks_append(disk_list *disks) {
    if (disks->count == disks->capacity) {
        disks->capacity = disks->capacity == 0 ? 4 : disks->capacity * 2;
        disks->items = memory_reallocate(disks->items, disks->capacity * sizeof *disks->items);
    }
    disk *added = &disks->items[disks->count++];
    memset(added, 0, sizeof *added);
    string_list_init(&added->labels);
    return added;
}

static char *trimmed_text(const cJSON *item) {
    if (!cJSON_IsString(item)) {
        return text_duplicate("");
    }
    const char *start = item->valuestring;
    while (*start == ' ') {
        start++;
    }
    size_t length = strlen(start);
    while (length > 0 && start[length - 1] == ' ') {
        length--;
    }
    return text_format("%.*s", (int)length, start);
}

/** @implNote lsblk before util-linux 2.33 wrote flags and sizes as strings, later ones as booleans and numbers. */
static bool flag_value(const cJSON *item) {
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble != 0;
    }
    return cJSON_IsString(item) && strcmp(item->valuestring, "1") == 0;
}

static unsigned long long size_value(const cJSON *item) {
    if (cJSON_IsNumber(item)) {
        return (unsigned long long)item->valuedouble;
    }
    return cJSON_IsString(item) ? strtoull(item->valuestring, NULL, 10) : 0;
}

static bool has_text(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring[0] != '\0';
}

static bool has_mount_point(const cJSON *device) {
    if (has_text(device, "mountpoint")) {
        return true;
    }
    const cJSON *mount_points = cJSON_GetObjectItemCaseSensitive(device, "mountpoints");
    const cJSON *point = NULL;
    cJSON_ArrayForEach(point, mount_points) {
        if (cJSON_IsString(point) && point->valuestring[0] != '\0') {
            return true;
        }
    }
    return false;
}

static void add_label(disk *target, const cJSON *device, const char *key) {
    if (has_text(device, key)) {
        string_list_add_unique(&target->labels, cJSON_GetObjectItemCaseSensitive(device, key)->valuestring);
    }
}

static void fold_device(disk *target, const cJSON *device) {
    target->has_filesystem = target->has_filesystem || has_text(device, "fstype");
    target->mounted = target->mounted || has_mount_point(device);
    add_label(target, device, "label");
    add_label(target, device, "partlabel");
    const cJSON *child = NULL;
    cJSON_ArrayForEach(child, cJSON_GetObjectItemCaseSensitive(device, "children")) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(child, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "part") == 0) {
            target->partition_count++;
        }
        fold_device(target, child);
    }
}

static void read_disk(disk_list *disks, const cJSON *device) {
    disk *added = disks_append(disks);
    added->name = trimmed_text(cJSON_GetObjectItemCaseSensitive(device, "name"));
    added->path = has_text(device, "path") ? trimmed_text(cJSON_GetObjectItemCaseSensitive(device, "path"))
                                           : text_format("/dev/%s", added->name);
    added->size = size_value(cJSON_GetObjectItemCaseSensitive(device, "size"));
    added->removable = flag_value(cJSON_GetObjectItemCaseSensitive(device, "rm"));
    added->read_only = flag_value(cJSON_GetObjectItemCaseSensitive(device, "ro"));
    added->transport = trimmed_text(cJSON_GetObjectItemCaseSensitive(device, "tran"));
    added->serial = trimmed_text(cJSON_GetObjectItemCaseSensitive(device, "serial"));
    added->model = trimmed_text(cJSON_GetObjectItemCaseSensitive(device, "model"));
    fold_device(added, device);
}

bool disks_parse_lsblk(const char *json, disk_list *disks, error_list *errors) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "blockdevices");
    if (!cJSON_IsArray(devices)) {
        error_push(errors, NULL, 0, "lsblk printed no list of block devices");
        cJSON_Delete(root);
        return false;
    }
    const cJSON *device = NULL;
    cJSON_ArrayForEach(device, devices) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(device, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "disk") == 0) {
            read_disk(disks, device);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool disks_read(command_runner *runner, disk_list *disks, error_list *errors) {
    char *output = NULL;
    int status = runner->capture(runner, LSBLK_ARGUMENTS, &output, errors);
    if (status != 0) {
        if (status > 0) {
            error_push(errors, NULL, 0, "lsblk failed with exit code %d: %s", status, output);
        }
        free(output);
        return false;
    }
    bool parsed = disks_parse_lsblk(output, disks, errors);
    free(output);
    return parsed;
}

const disk *disks_find(const disk_list *disks, const char *path) {
    for (size_t index = 0; path != NULL && index < disks->count; index++) {
        if (strcmp(disks->items[index].path, path) == 0) {
            return &disks->items[index];
        }
    }
    return NULL;
}

static bool is_internal_and_writable(const disk *candidate) {
    return !candidate->removable && !candidate->read_only && strcmp(candidate->transport, "usb") != 0 &&
           strncmp(candidate->name, "loop", 4) != 0 && strncmp(candidate->name, "zram", 4) != 0 &&
           strncmp(candidate->name, "sr", 2) != 0 && candidate->size > 0;
}

const disk *disks_default_target(const disk_list *disks, const char *stick_disk) {
    const disk *best = NULL;
    for (size_t index = 0; index < disks->count; index++) {
        const disk *candidate = &disks->items[index];
        bool is_stick = stick_disk != NULL && strcmp(candidate->path, stick_disk) == 0;
        if (is_stick || candidate->mounted || !is_internal_and_writable(candidate)) {
            continue;
        }
        if (best == NULL || candidate->size > best->size) {
            best = candidate;
        }
    }
    return best;
}

bool disk_holds_anything(const disk *candidate) {
    return candidate->partition_count > 0 || candidate->has_filesystem;
}

bool disk_holds_label(const disk *candidate, const char *label) {
    return string_list_contains(&candidate->labels, label);
}

bool disk_check_target(const disk_list *disks, const char *path, const char *stick_disk, error_list *errors) {
    const disk *candidate = disks_find(disks, path);
    if (candidate == NULL) {
        error_push(errors, NULL, 0, "%s is not a disk this computer lists; choose one of lsblk's disks", path);
        return false;
    }
    const char *refusal = NULL;
    if (stick_disk != NULL && strcmp(path, stick_disk) == 0) {
        refusal = "is the stick this install runs from";
    } else if (candidate->mounted) {
        refusal = "is mounted (it may hold the live system); unmount it first";
    } else if (candidate->read_only) {
        refusal = "is read-only";
    } else if (candidate->removable || strcmp(candidate->transport, "usb") == 0) {
        refusal = "is a removable USB disk, not an internal one";
    }
    if (refusal != NULL) {
        error_push(errors, NULL, 0, "%s %s: refusing to install onto it", path, refusal);
        return false;
    }
    return true;
}

bool disk_same_identity(const disk *before, const disk *now) {
    return before != NULL && now != NULL && strcmp(before->path, now->path) == 0 && before->size == now->size &&
           strcmp(before->serial, now->serial) == 0;
}

char *size_describe(unsigned long long bytes) {
    const char *const units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double value = (double)bytes;
    size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < COUNT_OF(units)) {
        value /= 1000.0;
        unit++;
    }
    return text_format(unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
}

char *disk_describe(const disk *candidate) {
    char *size = size_describe(candidate->size);
    char *description = text_format("%s, %s%s%s%s%s", candidate->path, size,
                                    candidate->model[0] != '\0' ? ", " : "", candidate->model,
                                    candidate->serial[0] != '\0' ? ", serial " : "", candidate->serial);
    free(size);
    return description;
}

static char *last_disk_line(const char *listing) {
    char *found = NULL;
    const char *line = listing;
    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        char *copy = text_format("%.*s", (int)length, line);
        char *space = strchr(copy, ' ');
        if (space != NULL && strcmp(space + 1, "disk") == 0) {
            *space = '\0';
            free(found);
            found = text_format("/dev/%s", copy);
        }
        free(copy);
        line = end == NULL ? NULL : end + 1;
    }
    return found;
}

char *disks_holding_directory(command_runner *runner, const char *directory, error_list *errors) {
    const char *const find_source[] = { "findmnt", "-n", "-o", "SOURCE", "--target", directory, NULL };
    char *source = NULL;
    if (runner->capture(runner, find_source, &source, errors) != 0) {
        free(source);
        return NULL;
    }
    source[strcspn(source, "\n")] = '\0';
    const char *const ancestors[] = { "lsblk", "-n", "-r", "-s", "-o", "NAME,TYPE", source, NULL };
    char *listing = NULL;
    char *found = NULL;
    if (runner->capture(runner, ancestors, &listing, errors) == 0) {
        found = last_disk_line(listing);
    }
    free(listing);
    free(source);
    return found;
}
