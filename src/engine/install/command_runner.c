#include "command_runner.h"

#include "platform.h"

#include <stdarg.h>
#include <stdlib.h>

static int real_run(command_runner *self, const char *const *arguments, error_list *errors) {
    return platform_run_streaming(arguments, self->log, errors);
}

static int real_capture(command_runner *self, const char *const *arguments, char **output, error_list *errors) {
    (void)self;
    return platform_run_capture(arguments, output, errors);
}

command_runner command_runner_real(FILE *log) {
    command_runner runner = { .run = real_run, .capture = real_capture, .log = log, .state = NULL };
    return runner;
}

char *command_line_text(const char *const *arguments) {
    char *line = text_duplicate("");
    for (size_t index = 0; arguments[index] != NULL; index++) {
        char *longer = text_format(index == 0 ? "%s%s" : "%s %s", line, arguments[index]);
        free(line);
        line = longer;
    }
    return line;
}

static void log_command(command_runner *runner, const char *const *arguments) {
    char *line = command_line_text(arguments);
    fprintf(runner->log, "$ %s\n", line);
    fflush(runner->log);
    free(line);
}

int runner_run(command_runner *runner, const char *const *arguments, error_list *errors) {
    log_command(runner, arguments);
    return runner->run(runner, arguments, errors);
}

int runner_capture(command_runner *runner, const char *const *arguments, char **output, error_list *errors) {
    log_command(runner, arguments);
    return runner->capture(runner, arguments, output, errors);
}

bool runner_succeeds(command_runner *runner, const char *const *arguments, error_list *errors) {
    return runner_run(runner, arguments, errors) == 0;
}

void runner_say(command_runner *runner, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *message = text_format_arguments(format, arguments);
    va_end(arguments);
    fprintf(runner->log, "[imprint] %s\n", message);
    fflush(runner->log);
    free(message);
}
