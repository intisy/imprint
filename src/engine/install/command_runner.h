#ifndef IMPRINT_COMMAND_RUNNER_H
#define IMPRINT_COMMAND_RUNNER_H

#include "error.h"
#include "text.h"

#include <stdio.h>

typedef struct command_runner command_runner;

/** Every external effect of an install goes through a runner, so tests can record it instead. */
struct command_runner {
    int (*run)(command_runner *self, const char *const *arguments, error_list *errors);
    int (*capture)(command_runner *self, const char *const *arguments, char **output, error_list *errors);
    FILE *log;
    void *state;
};

command_runner command_runner_real(FILE *log);

/** Logs "$ <command>" and runs it with its output passed on to the log. Returns the exit status or -1. */
int runner_run(command_runner *runner, const char *const *arguments, error_list *errors);

/** As runner_run, but the output goes to *output (set when the return is not -1) instead of the log. */
int runner_capture(command_runner *runner, const char *const *arguments, char **output, error_list *errors);

/** True when the command ran and exited 0. */
bool runner_succeeds(command_runner *runner, const char *const *arguments, error_list *errors);

void runner_say(command_runner *runner, const char *format, ...) IMPRINT_PRINTF(2, 3);

/** Joins the arguments with single spaces, for logs and for the test runner's record. */
char *command_line_text(const char *const *arguments);

#endif
