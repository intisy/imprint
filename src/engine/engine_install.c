#include "engine_install.h"

#include "engine.h"
#include "exit_codes.h"
#include "install.h"
#include "options.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

static const char DEFAULT_TARGET_ROOT[] = "/mnt";
static const char LIVE_GPG_DIRECTORY[] = "/etc/pacman.d/gnupg";

typedef struct install_paths {
    char *setup_file;
    char *data_directory;
    char *stick_root;
    char *personal_directory;
} install_paths;

static int usage(FILE *error_output, const char *problem, const char *word) {
    fprintf(error_output, "imprint-engine: %s '%s'\n\n%s", problem, word, ENGINE_USAGE);
    return IMPRINT_EXIT_USAGE;
}

static char *parent_directory(const char *path) {
    char *parent = text_duplicate(path);
    char *slash = strrchr(parent, '/');
    if (slash != NULL && slash != parent) {
        *slash = '\0';
    }
    return parent;
}

static char *absolute_or_default(const char *given, char *fallback, error_list *errors) {
    if (given == NULL) {
        return fallback;
    }
    free(fallback);
    return platform_absolute_path(given, errors);
}

/** @implNote The stick carries <stick>/imprint/<setup>.json, so the stick and the data repo default from its place. */
static bool resolve_paths(const char *setup_file, const named_option *options, install_paths *paths,
                          error_list *errors) {
    memset(paths, 0, sizeof *paths);
    paths->setup_file = platform_absolute_path(setup_file, errors);
    if (paths->setup_file == NULL) {
        return false;
    }
    char *bundle = parent_directory(paths->setup_file);
    paths->data_directory = absolute_or_default(options[0].value, path_join(bundle, "data"), errors);
    paths->stick_root = absolute_or_default(options[1].value, parent_directory(bundle), errors);
    paths->personal_directory = options[2].value == NULL ? NULL : platform_absolute_path(options[2].value, errors);
    free(bundle);
    return paths->data_directory != NULL && paths->stick_root != NULL &&
           (options[2].value == NULL || paths->personal_directory != NULL);
}

static void free_paths(install_paths *paths) {
    free(paths->setup_file);
    free(paths->data_directory);
    free(paths->stick_root);
    free(paths->personal_directory);
}

static ui *answers_screen(const char *answers_file, error_list *errors) {
    char *text = NULL;
    if (!file_read_all(answers_file, &text, errors)) {
        return NULL;
    }
    string_list answers;
    string_list_init(&answers);
    char *line = text;
    while (line != NULL && *line != '\0') {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        line[strcspn(line, "\r")] = '\0';
        string_list_add(&answers, line);
        line = end == NULL ? NULL : end + 1;
    }
    ui *screen = ui_scripted_create((const char *const *)answers.items, answers.count);
    string_list_free(&answers);
    free(text);
    return screen;
}

int engine_install_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output) {
    if (argument_count < 3) {
        return usage(error_output, "a resolved setup file must follow", "install");
    }
    named_option options[] = { { "--data", NULL }, { "--stick", NULL }, { "--personal", NULL },
                               { "--disk", NULL }, { "--answers", NULL }, { "--target", NULL },
                               { "--data-disk", NULL } };
    const char *problem_word = NULL;
    if (!options_read(arguments + 3, (size_t)(argument_count - 3), options, COUNT_OF(options), &problem_word)) {
        return usage(error_output, "unknown option or one without a value", problem_word);
    }
    error_list errors;
    error_list_init(&errors);
    install_paths paths;
    int exit_code = IMPRINT_EXIT_ENVIRONMENT;
    ui *screen = NULL;
    if (resolve_paths(arguments[2], options, &paths, &errors)) {
        screen = options[4].value != NULL ? answers_screen(options[4].value, &errors)
                                          : ui_ansi_create(stdin, output, ui_ansi_colors_available(output));
    }
    if (screen != NULL) {
        command_runner runner = command_runner_real(output);
        install_request request = { .setup_file = paths.setup_file, .data_directory = paths.data_directory,
                                    .stick_root = paths.stick_root, .personal_directory = paths.personal_directory,
                                    .target_root = options[5].value != NULL ? options[5].value : DEFAULT_TARGET_ROOT,
                                    .disk = options[3].value, .data_disk = options[6].value, .live_gpg_directory = LIVE_GPG_DIRECTORY,
                                    .screen = screen, .runner = &runner };
        exit_code = install_run(&request, &errors);
        ui_destroy(screen);
    }
    free_paths(&paths);
    error_list_print(&errors, error_output);
    error_list_free(&errors);
    return exit_code;
}
