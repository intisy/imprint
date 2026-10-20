#include "platform.h"

#include "text.h"

#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static DWORD attributes_of(const char *path) {
    return GetFileAttributesA(path);
}

bool platform_is_directory(const char *path) {
    DWORD attributes = attributes_of(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool platform_is_file(const char *path) {
    DWORD attributes = attributes_of(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool list_entries(const char *directory, bool want_directories, string_list *names, error_list *errors) {
    char *pattern = path_join(directory, "*");
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern, &found);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) {
        error_push(errors, directory, 0, "cannot open the directory: Windows error %lu", (unsigned long)GetLastError());
        return false;
    }
    do {
        bool is_directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (found.cFileName[0] != '.' && is_directory == want_directories) {
            string_list_add(names, found.cFileName);
        }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
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
    if (CreateDirectoryA(path, NULL) || (GetLastError() == ERROR_ALREADY_EXISTS && platform_is_directory(path))) {
        return true;
    }
    error_push(errors, path, 0, "cannot create the directory: Windows error %lu", (unsigned long)GetLastError());
    return false;
}

bool platform_set_mode(const char *path, unsigned int mode, error_list *errors) {
    (void)path;
    (void)mode;
    (void)errors;
    return true;
}

bool platform_make_symlink(const char *target, const char *link_path, error_list *errors) {
    error_push(errors, link_path, 0, "cannot link to %s: writing a Linux root needs Linux", target);
    return false;
}

char *platform_absolute_path(const char *path, error_list *errors) {
    char *resolved = _fullpath(NULL, path, 0);
    if (resolved == NULL || !(platform_is_file(resolved) || platform_is_directory(resolved))) {
        error_push(errors, path, 0, "cannot resolve the path: it does not exist");
        free(resolved);
        return NULL;
    }
    char *copy = text_duplicate(resolved);
    free(resolved);
    return copy;
}

char *platform_temporary_path(error_list *errors) {
    char directory[MAX_PATH + 1];
    char path[MAX_PATH + 1];
    DWORD length = GetTempPathA(sizeof directory, directory);
    if (length == 0 || length > sizeof directory || GetTempFileNameA(directory, "imp", 0, path) == 0) {
        error_push(errors, NULL, 0, "cannot create a temporary file: Windows error %lu", (unsigned long)GetLastError());
        return NULL;
    }
    return text_duplicate(path);
}

void platform_set_environment(const char *name, const char *value) {
    _putenv_s(name, value);
}

/**
 * @implNote tmpfile() on Windows creates its file in the root of the drive, which a normal user may
 * not write; "D" makes the C runtime delete the file when it is closed.
 */
FILE *platform_temporary_file(void) {
    char directory[MAX_PATH + 1];
    char path[MAX_PATH + 1];
    DWORD length = GetTempPathA(sizeof directory, directory);
    if (length == 0 || length > sizeof directory || GetTempFileNameA(directory, "imp", 0, path) == 0) {
        return NULL;
    }
    return fopen(path, "w+bD");
}

/**
 * @implNote The C runtime reads backslashes before a closing quote as escapes, so a trailing run is
 * doubled; a double quote or a percent sign would be reinterpreted by cmd, so those are refused.
 */
char *platform_quote_argument(const char *argument) {
    if (strpbrk(argument, "\"%\r\n") != NULL) {
        return NULL;
    }
    size_t length = strlen(argument);
    size_t trailing = 0;
    while (trailing < length && argument[length - 1 - trailing] == '\\') {
        trailing++;
    }
    char *quoted = memory_allocate(length + trailing + 3);
    quoted[0] = '"';
    memcpy(quoted + 1, argument, length);
    memset(quoted + 1 + length, '\\', trailing);
    quoted[1 + length + trailing] = '"';
    quoted[2 + length + trailing] = '\0';
    return quoted;
}

static char *command_line(const char *const *arguments, error_list *errors) {
    char *line = text_duplicate("");
    for (size_t index = 0; arguments[index] != NULL; index++) {
        char *quoted = platform_quote_argument(arguments[index]);
        if (quoted == NULL) {
            error_push(errors, NULL, 0, "cannot pass '%s' to %s: it holds a double quote or a percent sign",
                       arguments[index], arguments[0]);
            free(line);
            return NULL;
        }
        char *longer = index == 0 ? text_format("%s", quoted) : text_format("%s %s", line, quoted);
        free(quoted);
        free(line);
        line = longer;
    }
    return line;
}

/** @implNote cmd /c drops the first and the last quote of a line that starts with one, so the whole line is wrapped once more. */
static FILE *open_pipe(const char *const *arguments, error_list *errors) {
    char *line = command_line(arguments, errors);
    if (line == NULL) {
        return NULL;
    }
    char *wrapped = text_format("\"%s 2>&1\"", line);
    free(line);
    FILE *pipe = _popen(wrapped, "rb");
    free(wrapped);
    if (pipe == NULL) {
        error_push(errors, NULL, 0, "cannot start %s", arguments[0]);
    }
    return pipe;
}

static int exit_status(int status, const char *program, error_list *errors) {
    if (status == -1) {
        error_push(errors, NULL, 0, "cannot wait for %s", program);
    }
    return status;
}

int platform_run_capture(const char *const *arguments, char **output, error_list *errors) {
    FILE *pipe = open_pipe(arguments, errors);
    if (pipe == NULL) {
        return -1;
    }
    char *captured = platform_read_stream(pipe);
    int status = exit_status(_pclose(pipe), arguments[0], errors);
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
    int status = exit_status(_pclose(pipe), arguments[0], errors);
    if (!copied) {
        error_push(errors, NULL, 0, "cannot pass on the output of %s", arguments[0]);
        return -1;
    }
    return status;
}

bool platform_terminal_is_interactive(FILE *stream) {
    return _isatty(_fileno(stream)) != 0;
}

bool platform_set_echo(FILE *input, bool enabled) {
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(input));
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return false;
    }
    mode = enabled ? (mode | ENABLE_ECHO_INPUT) : (mode & ~(DWORD)ENABLE_ECHO_INPUT);
    return SetConsoleMode(handle, mode) != 0;
}
