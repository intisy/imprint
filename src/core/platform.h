#ifndef IMPRINT_PLATFORM_H
#define IMPRINT_PLATFORM_H

#include "error.h"
#include "string_list.h"

#include <stdbool.h>
#include <stdio.h>

bool platform_list_files(const char *directory, string_list *names, error_list *errors);
bool platform_list_directories(const char *directory, string_list *names, error_list *errors);
bool platform_is_directory(const char *path);
bool platform_is_file(const char *path);
bool platform_make_directory(const char *path, error_list *errors);

/** @implNote Does nothing on Windows, whose file systems keep no POSIX modes. */
bool platform_set_mode(const char *path, unsigned int mode, error_list *errors);

/** Replaces an existing link of that name. Windows refuses, since only a Linux root needs these links. */
bool platform_make_symlink(const char *target, const char *link_path, error_list *errors);
char *platform_absolute_path(const char *path, error_list *errors);
FILE *platform_temporary_file(void);

/** Creates an empty file under the system's temporary directory and returns its path. */
char *platform_temporary_path(error_list *errors);
void platform_set_environment(const char *name, const char *value);

/** Everything left in the stream, NUL terminated; NULL when the stream reports a read error. */
char *platform_read_stream(FILE *stream);
bool platform_copy_stream(FILE *from, FILE *to);
char *platform_quote_argument(const char *argument);

/**
 * Runs the NULL-terminated argument list through the shell with standard error joined to standard output.
 * Returns the exit status, or -1 with an error pushed when the command cannot start, is refused, or does
 * not exit normally. *output is set exactly when the return is not -1.
 */
int platform_run_capture(const char *const *arguments, char **output, error_list *errors);

/** As platform_run_capture, but passes the output on to the stream as it arrives. */
int platform_run_streaming(const char *const *arguments, FILE *output, error_list *errors);
bool platform_terminal_is_interactive(FILE *stream);

/** Turns the terminal's echo of typed characters on or off; false when the stream is no terminal. */
bool platform_set_echo(FILE *input, bool enabled);

#endif
