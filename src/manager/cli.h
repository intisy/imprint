#ifndef IMPRINT_CLI_H
#define IMPRINT_CLI_H

#include "error.h"
#include "ui.h"

#include <stdio.h>

typedef int (*command_runner)(const char *const *arguments, char **output, error_list *errors, void *state);

typedef struct cli_context {
    FILE *output;
    FILE *error_output;
    ui *interface;
    command_runner run;
    void *run_state;
} cli_context;

int cli_run(int argument_count, const char *const *arguments, cli_context *context);
int cli_platform_runner(const char *const *arguments, char **output, error_list *errors, void *state);
const char *cli_usage(void);

#endif
