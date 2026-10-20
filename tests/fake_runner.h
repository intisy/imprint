#ifndef IMPRINT_FAKE_RUNNER_H
#define IMPRINT_FAKE_RUNNER_H

#include "command_runner.h"
#include "string_list.h"

/** A runner that records each command line and answers from rules; an unmatched command exits 0 with no output. */
command_runner fake_runner_create(FILE *log);

/** Commands whose line starts with the prefix exit with the status and print the output; the latest rule wins. */
void fake_runner_answer(command_runner *runner, const char *command_prefix, int status, const char *output);
/** As fake_runner_answer, for the next `times` matching commands only. */
void fake_runner_answer_times(command_runner *runner, const char *command_prefix, int status, const char *output,
                              int times);
const string_list *fake_runner_commands(const command_runner *runner);
bool fake_runner_ran(const command_runner *runner, const char *command_line);

/** The index of the first recorded line starting with the prefix, or -1. */
long fake_runner_index(const command_runner *runner, const char *command_prefix);
size_t fake_runner_count(const command_runner *runner, const char *command_prefix);
void fake_runner_free(command_runner *runner);

#endif
