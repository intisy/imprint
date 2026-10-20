/** @implNote popen, opendir and fileno are POSIX and realpath is XSI, hidden by a strict -std=c17 unless asked for. */
#define _XOPEN_SOURCE 700

#include "platform.h"

#include "text.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

bool platform_is_directory(const char *path) {
    struct stat information;
    return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

bool platform_is_file(const char *path) {
    struct stat information;
    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static bool list_entries(const char *directory, bool want_directories, string_list *names, error_list *errors) {
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
        if (want_directories ? platform_is_directory(path) : platform_is_file(path)) {
            string_list_add(names, entry->d_name);
        }
        free(path);
    }
    closedir(handle);
    string_list_sort(names);
    return true;
}

bool platform_list_files(const char *directory, string_list *names, error_list *errors) {
    return list_entries(directory, false, names, errors);
}

bool platform_list_directories(const char *directory, string_list *names, error_list *errors) {
    return list_entries(directory, true, names, errors);
}

bool platform_make_directory(const char *path, error_list *errors) {
    if (mkdir(path, 0755) == 0 || (errno == EEXIST && platform_is_directory(path))) {
        return true;
    }
    error_push(errors, path, 0, "cannot create the directory: %s", strerror(errno));
    return false;
}

bool platform_set_mode(const char *path, unsigned int mode, error_list *errors) {
    if (chmod(path, (mode_t)mode) == 0) {
        return true;
    }
    error_push(errors, path, 0, "cannot set the mode %o: %s", mode, strerror(errno));
    return false;
}

bool platform_make_symlink(const char *target, const char *link_path, error_list *errors) {
    struct stat information;
    if (lstat(link_path, &information) == 0 && S_ISLNK(information.st_mode) && unlink(link_path) != 0) {
        error_push(errors, link_path, 0, "cannot replace the link: %s", strerror(errno));
        return false;
    }
    if (symlink(target, link_path) != 0) {
        error_push(errors, link_path, 0, "cannot link to %s: %s", target, strerror(errno));
        return false;
    }
    return true;
}

char *platform_absolute_path(const char *path, error_list *errors) {
    char *resolved = realpath(path, NULL);
    if (resolved == NULL) {
        error_push(errors, path, 0, "cannot resolve the path: %s", strerror(errno));
        return NULL;
    }
    char *copy = text_duplicate(resolved);
    free(resolved);
    return copy;
}

char *platform_temporary_path(error_list *errors) {
    const char *directory = getenv("TMPDIR");
    char *template = path_join(directory != NULL && directory[0] != '\0' ? directory : "/tmp", "imprint-XXXXXX");
    int descriptor = mkstemp(template);
    if (descriptor == -1) {
        error_push(errors, template, 0, "cannot create a temporary file: %s", strerror(errno));
        free(template);
        return NULL;
    }
    close(descriptor);
    return template;
}

void platform_set_environment(const char *name, const char *value) {
    setenv(name, value, 1);
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

static int exit_status(int status, const char *program, error_list *errors) {
    if (status == -1) {
        error_push(errors, NULL, 0, "cannot wait for %s: %s", program, strerror(errno));
        return -1;
    }
    if (!WIFEXITED(status)) {
        error_push(errors, NULL, 0, "%s did not exit normally", program);
        return -1;
    }
    return WEXITSTATUS(status);
}

static FILE *open_pipe(const char *const *arguments, error_list *errors) {
    char *line = command_line(arguments);
    char *joined = text_format("%s 2>&1", line);
    free(line);
    FILE *pipe = popen(joined, "r");
    free(joined);
    if (pipe == NULL) {
        error_push(errors, NULL, 0, "cannot start %s: %s", arguments[0], strerror(errno));
    }
    return pipe;
}

int platform_run_capture(const char *const *arguments, char **output, error_list *errors) {
    FILE *pipe = open_pipe(arguments, errors);
    if (pipe == NULL) {
        return -1;
    }
    char *captured = platform_read_stream(pipe);
    int status = exit_status(pclose(pipe), arguments[0], errors);
    if (captured == NULL) {
        error_push(errors, NULL, 0, "cannot read the output of %s", arguments[0]);
        return -1;
    }
    if (status == -1) {
        free(captured);
        return -1;
    }
    *output = captured;
    return status;
}

int platform_run_streaming(const char *const *arguments, FILE *output, error_list *errors) {
    fflush(output);
    FILE *pipe = open_pipe(arguments, errors);
    if (pipe == NULL) {
        return -1;
    }
    bool copied = platform_copy_stream(pipe, output);
    int status = exit_status(pclose(pipe), arguments[0], errors);
    if (!copied) {
        error_push(errors, NULL, 0, "cannot pass on the output of %s", arguments[0]);
        return -1;
    }
    return status;
}

bool platform_terminal_is_interactive(FILE *stream) {
    return isatty(fileno(stream)) != 0;
}

bool platform_set_echo(FILE *input, bool enabled) {
    struct termios settings;
    int descriptor = fileno(input);
    if (!isatty(descriptor) || tcgetattr(descriptor, &settings) != 0) {
        return false;
    }
    if (enabled) {
        settings.c_lflag |= (tcflag_t)ECHO;
    } else {
        settings.c_lflag &= ~(tcflag_t)ECHO;
    }
    return tcsetattr(descriptor, TCSANOW, &settings) == 0;
}
