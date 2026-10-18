#ifndef IMPRINT_CLI_HARNESS_H
#define IMPRINT_CLI_HARNESS_H

#include "cli.h"

typedef struct cli_result {
    int exit_code;
    char *output;
    char *error_output;
} cli_result;

cli_result cli_harness_run(const char *const *arguments, size_t count, ui *interface, command_runner run,
                           void *run_state);
void cli_result_free(cli_result *result);
int cli_harness_refusing_runner(const char *const *arguments, char **output, error_list *errors, void *state);

#endif
