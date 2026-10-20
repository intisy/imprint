#include "engine_hooks.h"

#include "engine.h"
#include "exit_codes.h"
#include "firstboot.h"
#include "options.h"
#include "platform.h"
#include "resolve.h"

#include <stdlib.h>
#include <string.h>

typedef struct hook_paths {
    char *setup_file;
    char *data_directory;
    char *personal_directory;
} hook_paths;

typedef struct loaded_hooks {
    char *setup_json;
    resolved_setup resolved;
    hook_list hooks;
} loaded_hooks;

static int usage(FILE *error_output, const char *problem, const char *word) {
    fprintf(error_output, "imprint-engine: %s '%s'\n\n%s", problem, word, ENGINE_USAGE);
    return IMPRINT_EXIT_USAGE;
}

static bool absolute_or_null(const char *path, char **absolute, error_list *errors) {
    *absolute = path == NULL ? NULL : platform_absolute_path(path, errors);
    return path == NULL || *absolute != NULL;
}

static bool resolve_paths(const char *setup_file, const char *data, const char *personal, hook_paths *paths,
                          error_list *errors) {
    memset(paths, 0, sizeof *paths);
    bool resolved = absolute_or_null(setup_file, &paths->setup_file, errors);
    resolved = absolute_or_null(data, &paths->data_directory, errors) && resolved;
    return absolute_or_null(personal, &paths->personal_directory, errors) && resolved;
}

static void free_paths(hook_paths *paths) {
    free(paths->setup_file);
    free(paths->data_directory);
    free(paths->personal_directory);
}

static bool load_hooks(const char *path, loaded_hooks *loaded, error_list *errors) {
    resolved_setup_init(&loaded->resolved);
    hook_list_init(&loaded->hooks);
    loaded->setup_json = NULL;
    return file_read_all(path, &loaded->setup_json, errors) &&
           resolved_setup_from_json(loaded->setup_json, path, &loaded->resolved, errors) &&
           hooks_read(loaded->resolved.setup, &loaded->hooks, errors);
}

static void free_hooks(loaded_hooks *loaded) {
    free(loaded->setup_json);
    resolved_setup_free(&loaded->resolved);
    hook_list_free(&loaded->hooks);
}

static int finish(FILE *error_output, error_list *errors, int exit_code) {
    error_list_print(errors, error_output);
    error_list_free(errors);
    return exit_code;
}

static int status_to_exit_code(int status) {
    return status < 0 ? IMPRINT_EXIT_ENVIRONMENT : status;
}

static int run_stage(const char *stage_name, const char *setup_file, const named_option *options, FILE *output,
                     FILE *error_output) {
    hook_stage stage = HOOK_STAGE_BUILD;
    if (!hook_stage_parse(stage_name, &stage)) {
        return usage(error_output, "unknown hook stage", stage_name);
    }
    error_list errors;
    error_list_init(&errors);
    hook_paths paths;
    loaded_hooks loaded;
    if (!resolve_paths(setup_file, options[0].value, options[1].value, &paths, &errors)) {
        free_paths(&paths);
        return finish(error_output, &errors, IMPRINT_EXIT_ENVIRONMENT);
    }
    int exit_code = IMPRINT_EXIT_INVALID;
    if (load_hooks(paths.setup_file, &loaded, &errors)) {
        hook_environment environment = { .setup_name = loaded.resolved.name, .setup_file = paths.setup_file,
                                         .data_directory = paths.data_directory,
                                         .personal_directory = paths.personal_directory, .log = output };
        exit_code = status_to_exit_code(hooks_run_stage(&loaded.hooks, stage, &environment, &errors));
    }
    free_hooks(&loaded);
    free_paths(&paths);
    return finish(error_output, &errors, exit_code);
}

static int install_firstboot(const char *setup_file, const named_option *options, FILE *output, FILE *error_output) {
    error_list errors;
    error_list_init(&errors);
    hook_paths paths;
    loaded_hooks loaded;
    if (!resolve_paths(setup_file, options[0].value, options[1].value, &paths, &errors)) {
        free_paths(&paths);
        return finish(error_output, &errors, IMPRINT_EXIT_ENVIRONMENT);
    }
    int exit_code = IMPRINT_EXIT_INVALID;
    if (load_hooks(paths.setup_file, &loaded, &errors)) {
        firstboot_request request = { .target_root = options[2].value, .setup_name = loaded.resolved.name,
                                      .setup_json = loaded.setup_json, .hooks = &loaded.hooks,
                                      .data_directory = paths.data_directory,
                                      .personal_directory = paths.personal_directory };
        bool installed = false;
        exit_code = firstboot_install(&request, &installed, &errors) ? IMPRINT_EXIT_OK : IMPRINT_EXIT_ENVIRONMENT;
        firstboot_report(loaded.resolved.name, options[2].value, installed, exit_code == IMPRINT_EXIT_OK, output);
    }
    free_hooks(&loaded);
    free_paths(&paths);
    return finish(error_output, &errors, exit_code);
}

int engine_hooks_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output) {
    if (argument_count < 4) {
        return usage(error_output, "missing arguments for", "hooks");
    }
    bool is_run = strcmp(arguments[2], "run") == 0;
    if (!is_run && strcmp(arguments[2], "install-firstboot") != 0) {
        return usage(error_output, "unknown hooks command", arguments[2]);
    }
    int first_option = is_run ? 5 : 4;
    if (argument_count < first_option) {
        return usage(error_output, "missing arguments for", arguments[2]);
    }
    named_option options[] = { { "--data", NULL }, { "--personal", NULL }, { "--target", NULL } };
    const char *problem_word = NULL;
    size_t option_count = is_run ? 2 : 3;
    if (!options_read(arguments + first_option, (size_t)(argument_count - first_option), options, option_count,
                      &problem_word)) {
        return usage(error_output, "unknown option or one without a value", problem_word);
    }
    if (options[0].value == NULL || (!is_run && options[2].value == NULL)) {
        return usage(error_output, "a required option is missing for", arguments[2]);
    }
    return is_run ? run_stage(arguments[3], arguments[4], options, output, error_output)
                  : install_firstboot(arguments[3], options, output, error_output);
}
