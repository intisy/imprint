#ifndef IMPRINT_BOOTLOADER_H
#define IMPRINT_BOOTLOADER_H

#include "command_runner.h"
#include "install_options.h"

extern const char BOOTLOADER_ID[];

/**
 * GRUB gets a firmware entry and the removable path EFI/BOOT/BOOTX64.EFI, so a firmware reset of boot entries does not
 * strand the install. Only a missing removable path fails: without the entry the disk still boots.
 */
bool bootloader_install(command_runner *runner, const install_options *options, const char *target_root,
                        error_list *errors);

#endif
