#ifndef IMPRINT_COMMANDS_H
#define IMPRINT_COMMANDS_H

#include "cli.h"

#include <stdbool.h>

int command_setups(const char *root, cli_context *context);
int command_setup_show(const char *root, const char *name, bool as_json, cli_context *context);
int command_validate(const char *root, cli_context *context);
int command_interactive(const char *root, cli_context *context);
int command_data_use(const char *root, const char *source, cli_context *context);
int command_catalog_update(const char *root, cli_context *context);
int command_hooks_run(const char *root, const char *stage_name, const char *name, const char *personal,
                      cli_context *context);
int command_hooks_install_firstboot(const char *root, const char *name, const char *target, const char *personal,
                                    cli_context *context);
/** Writes what a stick carries in imprint/ for the setup: <name>.json, data/ and, given one, the engine binary. */
int command_bundle(const char *root, const char *name, const char *directory, const char *engine, cli_context *context);
int command_finish(cli_context *context, error_list *errors, int exit_code);

#endif
