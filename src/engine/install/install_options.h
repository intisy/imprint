#ifndef IMPRINT_INSTALL_OPTIONS_H
#define IMPRINT_INSTALL_OPTIONS_H

#include "resolve.h"

typedef enum root_filesystem { ROOT_EXT4, ROOT_BTRFS } root_filesystem;

typedef struct setup_user {
    char *name;
    char *password_hash;
    char *shell;
    bool sudo;
} setup_user;

typedef struct install_options {
    char *disk;
    char *hostname;
    char *username;
    char *password;
    char *password_hash;
    char *timezone;
    char *locale;
    char *keyboard_layout;
    char *display_manager;
    char *bootloader;
    char *aur_helper;
    bool expire_password;
    long long esp_size_mb;
    long long swap_size_mb;
    long long root_size_mb;
    root_filesystem root_fs;
    char *data_label;
    char *data_disk;
    char *data_mount;
    char *data_filesystem;
    char *offered_data_label;
    long long offered_root_size_mb;
    string_list user_groups;
    string_list authorized_keys;
    string_list notes;
    setup_user *users;
    size_t user_count;
} install_options;

void install_options_init(install_options *options);
void install_options_free(install_options *options);

/** The install values of the merged setup ([install], [disk], [system], [remote], [[users]]) with engine.sh's defaults. */
bool install_options_read(const resolved_setup *resolved, install_options *options, error_list *errors);

/**
 * @implNote VD-10: a greeter such as SDDM cannot run the password change PAM asks for after `chage -d 0` and refuses
 * the login, so an expired password is only safe on a system without a display manager.
 */
bool install_options_should_expire_password(const install_options *options);
bool install_options_have_display_manager(const install_options *options);

/** Sets the data partition on or off; off gives root the rest of the disk, on restores the setup's layout. */
void install_options_set_data_partition(install_options *options, bool enabled);

/** Puts the data partition on a whole second disk; root then takes the rest of the system disk. */
void install_options_use_data_disk(install_options *options, const char *data_disk);
void install_options_replace(char **field, const char *value);
const char *root_filesystem_name(root_filesystem filesystem);

#endif
