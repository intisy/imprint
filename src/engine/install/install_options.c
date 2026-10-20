#include "install_options.h"

#include <stdlib.h>
#include <string.h>

enum {
    DEFAULT_ESP_SIZE_MB = 512,
    DEFAULT_SWAP_SIZE_MB = 8192
};

void install_options_init(install_options *options) {
    memset(options, 0, sizeof *options);
    string_list_init(&options->user_groups);
    string_list_init(&options->authorized_keys);
    string_list_init(&options->notes);
}

void install_options_free(install_options *options) {
    char **texts[] = { &options->disk, &options->hostname, &options->username, &options->password,
                       &options->password_hash, &options->timezone, &options->locale, &options->keyboard_layout,
                       &options->display_manager, &options->bootloader, &options->aur_helper, &options->data_label,
                       &options->data_disk,
                       &options->data_mount, &options->data_filesystem, &options->offered_data_label };
    for (size_t index = 0; index < COUNT_OF(texts); index++) {
        free(*texts[index]);
    }
    for (size_t index = 0; index < options->user_count; index++) {
        free(options->users[index].name);
        free(options->users[index].password_hash);
        free(options->users[index].shell);
    }
    free(options->users);
    string_list_free(&options->user_groups);
    string_list_free(&options->authorized_keys);
    string_list_free(&options->notes);
    install_options_init(options);
}

void install_options_replace(char **field, const char *value) {
    free(*field);
    *field = value == NULL ? NULL : text_duplicate(value);
}

const char *root_filesystem_name(root_filesystem filesystem) {
    return filesystem == ROOT_BTRFS ? "btrfs" : "ext4";
}

static bool read_text(const document_node *table, const char *key, const char *fallback, char **field,
                      error_list *errors) {
    const char *value = NULL;
    if (!document_optional_string(table, key, fallback, &value, errors)) {
        return false;
    }
    install_options_replace(field, value);
    return true;
}

static const char *display_manager_for(const resolved_setup *resolved) {
    if (resolved->window_managers.count == 0) {
        return NULL;
    }
    const char *first = resolved->window_managers.items[0];
    if (strcmp(first, "i3") == 0) {
        return "lightdm";
    }
    return strcmp(first, "gnome") == 0 ? "gdm" : "sddm";
}

static bool read_users(const document_node *setup_table, install_options *options, error_list *errors) {
    const document_node *users = document_get(setup_table, "users");
    if (users == NULL) {
        return true;
    }
    options->users = memory_allocate((users->child_count + 1) * sizeof *options->users);
    bool valid = true;
    for (size_t index = 0; index < users->child_count; index++) {
        const document_node *entry = users->children[index];
        setup_user *user = &options->users[options->user_count];
        memset(user, 0, sizeof *user);
        bool complete = read_text(entry, "name", NULL, &user->name, errors) &&
                        read_text(entry, "password_hash", NULL, &user->password_hash, errors) &&
                        read_text(entry, "shell", NULL, &user->shell, errors) &&
                        document_optional_boolean(entry, "sudo", false, &user->sudo, errors);
        options->user_count++;
        if (complete && user->name == NULL) {
            document_error(errors, entry, "a user needs a 'name'");
            complete = false;
        }
        valid = complete && valid;
    }
    return valid;
}

static bool read_install(const document_node *install, const resolved_setup *resolved, install_options *options,
                         error_list *errors) {
    const char *first_user = NULL;
    const document_node *users = document_get(resolved->setup, "users");
    if (users != NULL && users->child_count > 0) {
        const document_node *name = document_get(users->children[0], "name");
        first_user = name != NULL && name->kind == DOCUMENT_STRING ? name->string_value : NULL;
    }
    return read_text(install, "hostname", resolved->name, &options->hostname, errors) &&
           read_text(install, "timezone", "UTC", &options->timezone, errors) &&
           read_text(install, "locale", "en_US.UTF-8", &options->locale, errors) &&
           read_text(install, "username", first_user == NULL ? "user" : first_user, &options->username, errors) &&
           read_text(install, "password_hash", NULL, &options->password_hash, errors) &&
           document_optional_boolean(install, "expire_password", false, &options->expire_password, errors);
}

static bool read_disk(const document_node *disk_table, install_options *options, error_list *errors) {
    const char *root_fs = NULL;
    const document_node *data = NULL;
    bool valid = document_optional_integer(disk_table, "esp_size_mb", DEFAULT_ESP_SIZE_MB, &options->esp_size_mb, errors) &&
                 document_optional_integer(disk_table, "swap_size_mb", DEFAULT_SWAP_SIZE_MB, &options->swap_size_mb, errors) &&
                 document_optional_integer(disk_table, "root_size_mb", 0, &options->root_size_mb, errors) &&
                 document_optional_string(disk_table, "root_fs", "ext4", &root_fs, errors) &&
                 document_optional_table(disk_table, "data_partition", &data, errors);
    if (!valid) {
        return false;
    }
    if (strcmp(root_fs, "ext4") != 0 && strcmp(root_fs, "btrfs") != 0) {
        document_error(errors, document_get(disk_table, "root_fs"), "root_fs is ext4 or btrfs, not '%s'", root_fs);
        return false;
    }
    options->root_fs = strcmp(root_fs, "btrfs") == 0 ? ROOT_BTRFS : ROOT_EXT4;
    options->offered_root_size_mb = options->root_size_mb;
    if (data == NULL) {
        return true;
    }
    if (!read_text(data, "label", "data", &options->offered_data_label, errors) ||
        !read_text(data, "mount_point", "/data", &options->data_mount, errors) ||
        !read_text(data, "filesystem", "ext4", &options->data_filesystem, errors)) {
        return false;
    }
    if (options->root_size_mb <= 0) {
        document_error(errors, data, "a data partition needs [disk] root_size_mb, since it takes the rest of the disk");
        return false;
    }
    install_options_set_data_partition(options, true);
    return true;
}

static bool read_system(const document_node *setup_table, install_options *options, error_list *errors) {
    const document_node *system = NULL;
    const document_node *remote = NULL;
    bool valid = document_optional_table(setup_table, "system", &system, errors) &&
                 document_optional_table(setup_table, "remote", &remote, errors);
    if (!valid) {
        return false;
    }
    valid = system == NULL || (read_text(system, "keyboard_layout", NULL, &options->keyboard_layout, errors) &&
                               document_string_list(system, "user_groups", &options->user_groups, errors));
    valid = (remote == NULL || document_string_list(remote, "ssh_authorized_keys", &options->authorized_keys, errors)) &&
            valid;
    return document_string_list(setup_table, "notes", &options->notes, errors) && valid;
}

bool install_options_read(const resolved_setup *resolved, install_options *options, error_list *errors) {
    install_options_init(options);
    const document_node *setup_table = resolved->setup;
    const document_node *install = NULL;
    const document_node *disk_table = NULL;
    bool valid = document_optional_table(setup_table, "install", &install, errors) &&
                 document_optional_table(setup_table, "disk", &disk_table, errors);
    if (!valid) {
        return false;
    }
    valid = read_install(install, resolved, options, errors);
    valid = read_disk(disk_table, options, errors) && valid;
    valid = read_system(setup_table, options, errors) && valid;
    valid = read_users(setup_table, options, errors) && valid;
    valid = read_text(setup_table, "display_manager", display_manager_for(resolved), &options->display_manager, errors) &&
            valid;
    valid = read_text(setup_table, "aur_helper", "yay", &options->aur_helper, errors) && valid;
    install_options_replace(&options->bootloader, resolved->bootloader);
    if (resolved->window_managers.count == 0) {
        install_options_replace(&options->display_manager, NULL);
    }
    return valid;
}

bool install_options_have_display_manager(const install_options *options) {
    return options->display_manager != NULL && options->display_manager[0] != '\0';
}

bool install_options_should_expire_password(const install_options *options) {
    return options->expire_password && !install_options_have_display_manager(options);
}

void install_options_use_data_disk(install_options *options, const char *data_disk) {
    install_options_set_data_partition(options, options->offered_data_label != NULL);
    if (options->data_label != NULL) {
        install_options_replace(&options->data_disk, data_disk);
        options->root_size_mb = 0;
    }
}

void install_options_set_data_partition(install_options *options, bool enabled) {
    install_options_replace(&options->data_disk, NULL);
    if (enabled && options->offered_data_label != NULL) {
        install_options_replace(&options->data_label, options->offered_data_label);
        options->root_size_mb = options->offered_root_size_mb;
    } else {
        install_options_replace(&options->data_label, NULL);
        options->root_size_mb = 0;
    }
}
