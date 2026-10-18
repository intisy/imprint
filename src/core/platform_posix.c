/** @implNote popen, opendir and fileno are POSIX, hidden by a strict -std=c17 unless asked for. */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include "text.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

bool platform_is_directory(const char *path) {
    struct stat information;
    return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

bool platform_is_file(const char *path) {
    struct stat information;
    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

bool platform_list_files(const char *directory, string_list *names, error_list *errors) {
    DIR *handle = opendir(directory);
    if (handle == NULL) {
        error_push(errors, directory, 0, "cannot open the directory: %s", strerror(errno));
        return false;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char *path = path_join(directory, entry->d_name);
        if (platform_is_file(path)) {
            string_list_add(names, entry->d_name);
        }
        free(path);
    }
    closedir(handle);
    string_list_sort(names);
    return true;
}

FILE *platform_temporary_file(void) {
    return tmpfile();
}

char *platform_quote_argument(const char *argument) {
    size_t length = 3;
    for (const char *character = argument; *character != '\0'; character++) {
        length += *character == '\'' ? 4 : 1;
    }
    char *quoted = memory_allocate(length);
    char *out = quoted;
    *out++ = '\'';
    for (const char *character = argument; *character != '\0'; character++) {
        if (*character == '\'') {
            memcpy(out, "'\\''", 4);
            out += 4;
        } else {
            *out++ = *character;
        }
    }
    *out++ = '\'';
    *out = '\0';
    return quoted;
}

static char *command_line(const char *const *arguments) {
    char *line = text_duplicate("");
    for (size_t index = 0; arguments[index] != NULL; index++) {
        char *quoted = platform_quote_argument(arguments[index]);
        char *longer = index == 0 ? text_format("%s", quoted) : text_format("%s %s", line, quoted);
        free(quoted);
        free(line);
        line = longer;
    }
    return line;
}

int platform_run_capture(const char *const *arguments, char **output, error_list *errors) {
    char *line = command_line(arguments);
    char *joined = text_format("%s 2>&1", line);
    free(line);
    FILE *pipe = popen(joined, "r");
    free(joined);
    if (pipe == NULL) {
        error_push(errors, NULL, 0, "cannot start %s: %s", arguments[0], strerror(errno));
        return -1;
    }
    char *captured = platform_read_stream(pipe);
    int status = pclose(pipe);
    if (captured == NULL) {
        error_push(errors, NULL, 0, "cannot read the output of %s", arguments[0]);
        return -1;
    }
    if (status == -1) {
        free(captured);
        error_push(errors, NULL, 0, "cannot wait for %s: %s", arguments[0], strerror(errno));
        return -1;
    }
    if (!WIFEXITED(status)) {
        free(captured);
        error_push(errors, NULL, 0, "%s did not exit normally", arguments[0]);
        return -1;
    }
    *output = captured;
    return WEXITSTATUS(status);
}

bool platform_terminal_is_interactive(FILE *stream) {
    return isatty(fileno(stream)) != 0;
}
