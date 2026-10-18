#ifndef IMPRINT_PLATFORM_H
#define IMPRINT_PLATFORM_H

#include "error.h"
#include "string_list.h"

#include <stdbool.h>
#include <stdio.h>

bool platform_list_files(const char *directory, string_list *names, error_list *errors);
bool platform_is_directory(const char *path);
bool platform_is_file(const char *path);
FILE *platform_temporary_file(void);

/** Everything left in the stream, NUL terminated; NULL when the stream reports a read error. */
char *platform_read_stream(FILE *stream);
char *platform_quote_argument(const char *argument);

/**
 * Runs the NULL-terminated argument list through the shell with standard error joined to standard output.
 * Returns the exit status, or -1 with an error pushed when the command cannot start, is refused, or does
 * not exit normally. *output is set exactly when the return is not -1.
 */
int platform_run_capture(const char *const *arguments, char **output, error_list *errors);
bool platform_terminal_is_interactive(FILE *stream);

#endif
