#include "bootloader.h"

#include "base_system.h"

#include <string.h>

const char BOOTLOADER_ID[] = "imprint";

static bool install_grub(command_runner *runner, const char *target_root, error_list *errors) {
    const char *const with_entry[] = { "grub-install", "--target=x86_64-efi", "--efi-directory=/boot",
                                       "--bootloader-id=imprint", NULL };
    const char *const removable[] = { "grub-install", "--target=x86_64-efi", "--efi-directory=/boot", "--removable",
                                      NULL };
    const char *const configure[] = { "grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL };
    if (!target_run(runner, target_root, with_entry, errors)) {
        runner_say(runner, "bootloader: no firmware entry was written; the removable path still boots the disk");
    }
    return target_run(runner, target_root, removable, errors) && target_run(runner, target_root, configure, errors);
}

bool bootloader_install(command_runner *runner, const install_options *options, const char *target_root,
                        error_list *errors) {
    runner_say(runner, "installing the bootloader: %s", options->bootloader);
    bool installed = false;
    if (strcmp(options->bootloader, "grub") == 0) {
        installed = install_grub(runner, target_root, errors);
    } else if (strcmp(options->bootloader, "systemd-boot") == 0) {
        const char *const install[] = { "bootctl", "install", NULL };
        installed = target_run(runner, target_root, install, errors);
    } else if (strcmp(options->bootloader, "refind") == 0) {
        const char *const install[] = { "refind-install", NULL };
        installed = target_run(runner, target_root, install, errors);
    }
    if (!installed) {
        error_push(errors, NULL, 0, "the bootloader %s could not be installed; the disk will not boot",
                   options->bootloader);
    }
    return installed;
}
