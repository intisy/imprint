#ifndef IMPRINT_INSTALL_H
#define IMPRINT_INSTALL_H

#include "command_runner.h"
#include "disks.h"
#include "install_options.h"
#include "ui.h"

typedef struct install_request {
    const char *setup_file;
    const char *data_directory;
    const char *stick_root;
    const char *personal_directory;
    const char *target_root;
    const char *disk;
    const char *data_disk;
    const char *live_gpg_directory;
    ui *screen;
    command_runner *runner;
} install_request;

/**
 * The review screen: every value can be changed, and the install starts only with a password set and a disk that
 * passes the guards. False when the person cancels.
 */
bool install_review(ui *screen, install_options *options, const disk_list *disks, const char *stick_disk);

/** Asks for the disk's name when it holds anything; false when the typed name does not match. */
bool install_confirm_erase(ui *screen, const disk *target, const char *data_label);

/** The data disk passes the guards and is not the system disk. */
bool install_check_data_disk(const install_options *options, const disk_list *disks, const char *stick_disk,
                             error_list *errors);

/** Installs the resolved setup in request->setup_file onto a disk; returns an imprint exit code. */
int install_run(const install_request *request, error_list *errors);

#endif
