#ifndef IMPRINT_BASE_SYSTEM_H
#define IMPRINT_BASE_SYSTEM_H

#include "command_runner.h"
#include "install_options.h"
#include "offline.h"

/** What pacstrap installs; the stick's offline repository is built from the same list by ventoy-deploy. */
void base_packages(const install_options *options, string_list *packages);

/** pacstrap from the stick's repository alone, then from the mirrors in three attempts when that falls short. */
bool base_bootstrap(command_runner *runner, const install_options *options, const offline_stick *stick,
                    const char *target_root, error_list *errors);
bool base_write_fstab(command_runner *runner, const char *target_root, error_list *errors);

/** Hostname, time zone, locale, keyboard, the user with its password, sudo for wheel, NetworkManager, root locked. */
bool base_configure(command_runner *runner, const install_options *options, const char *target_root,
                    error_list *errors);

/** The setup's extra groups, after the packages that create them are installed; a missing group is skipped. */
void base_apply_user_groups(command_runner *runner, const install_options *options, const char *target_root,
                            error_list *errors);

/** Writes "<name>:<secret>" where only root can read it and feeds it to chpasswd, so it is never on a command line. */
bool base_set_password(command_runner *runner, const char *target_root, const char *name, const char *secret,
                       bool is_hash, error_list *errors);

/** Writes a file into the target root, creating its directories. */
bool target_write(const char *target_root, const char *inside, const char *text, error_list *errors);
bool target_append(const char *target_root, const char *inside, const char *text, error_list *errors);
char *target_path(const char *target_root, const char *inside);

/** Runs the command inside the target with arch-chroot; true when it exited 0. */
bool target_run(command_runner *runner, const char *target_root, const char *const *command, error_list *errors);

#endif
