#ifndef IMPRINT_SOFTWARE_H
#define IMPRINT_SOFTWARE_H

#include "command_runner.h"
#include "install_options.h"
#include "offline.h"

typedef struct software_request {
    const install_options *options;
    const resolved_setup *resolved;
    const offline_stick *stick;
    const char *data_directory;
    const char *target_root;
} software_request;

/**
 * Installs the setup's software into the target and configures it for the user. A package that fails names itself and
 * the rest goes on; false only when the repository packages fail every attempt, with *problems listing every failure.
 */
bool software_install(command_runner *runner, const software_request *request, string_list *problems,
                      error_list *errors);

void display_manager_packages(const char *display_manager, string_list *packages);
char *build_sudoers_path(const char *target_root);

#endif
