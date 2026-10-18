#include "cli.h"
#include "platform.h"
#include "ui.h"

#include <stdio.h>

int main(int argument_count, char **arguments) {
    ui *interface = NULL;
    if (platform_terminal_is_interactive(stdin) && platform_terminal_is_interactive(stdout)) {
        interface = ui_ansi_create(stdin, stdout, ui_ansi_colors_available(stdout));
    }
    cli_context context = { .output = stdout, .error_output = stderr, .interface = interface,
                            .run = cli_platform_runner, .run_state = NULL };
    int exit_code = cli_run(argument_count, (const char *const *)arguments, &context);
    if (interface != NULL) {
        ui_destroy(interface);
    }
    return exit_code;
}
