#include "cli_harness.h"

#include "platform.h"

#include <stdlib.h>

int cli_harness_refusing_runner(const char *const *arguments, char **output, error_list *errors, void *state) {
    (void)output;
    (void)state;
    error_push(errors, NULL, 0, "no command may run in this test, and %s was asked for", arguments[0]);
    return -1;
}

cli_result cli_harness_run(const char *const *arguments, size_t count, ui *interface, command_runner run,
                           void *run_state) {
    FILE *output = platform_temporary_file();
    FILE *error_output = platform_temporary_file();
    cli_context context = { .output = output, .error_output = error_output, .interface = interface,
                            .run = run, .run_state = run_state };
    cli_result result;
    result.exit_code = cli_run((int)count, arguments, &context);
    rewind(output);
    rewind(error_output);
    result.output = platform_read_stream(output);
    result.error_output = platform_read_stream(error_output);
    fclose(output);
    fclose(error_output);
    return result;
}

void cli_result_free(cli_result *result) {
    free(result->output);
    free(result->error_output);
}
