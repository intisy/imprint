#include "commands.h"

#include "catalog.h"
#include "exit_codes.h"
#include "file_tree.h"
#include "firstboot.h"
#include "identifier.h"
#include "platform.h"
#include "resolve.h"
#include "setup.h"
#include "workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct prepared_setup {
    workspace opened;
    catalog available;
    bool catalog_loaded;
    setup loaded;
    resolved_setup resolved;
    hook_list hooks;
    char *json;
    char *data_directory;
    char *personal_directory;
} prepared_setup;

static void prepared_init(prepared_setup *prepared) {
    memset(prepared, 0, sizeof *prepared);
    resolved_setup_init(&prepared->resolved);
    hook_list_init(&prepared->hooks);
}

static void prepared_free(prepared_setup *prepared) {
    free(prepared->json);
    free(prepared->data_directory);
    free(prepared->personal_directory);
    hook_list_free(&prepared->hooks);
    resolved_setup_free(&prepared->resolved);
    setup_free(&prepared->loaded);
    if (prepared->catalog_loaded) {
        catalog_free(&prepared->available);
    }
    workspace_close(&prepared->opened);
}

static bool resolve_setup(const char *name, prepared_setup *prepared, error_list *errors) {
    prepared->catalog_loaded = true;
    if (!catalog_load(prepared->opened.catalog_directory, prepared->opened.data_directory, &prepared->available, errors) ||
        !setup_load(prepared->opened.data_directory, name, &prepared->loaded, errors) ||
        !setup_resolve(&prepared->loaded, &prepared->available, &prepared->resolved, errors) ||
        !hooks_read(prepared->resolved.setup, &prepared->hooks, errors)) {
        return false;
    }
    prepared->json = resolved_setup_to_json(&prepared->resolved);
    return true;
}

static bool resolve_paths(const char *personal, prepared_setup *prepared, error_list *errors) {
    prepared->data_directory = platform_absolute_path(prepared->opened.data_directory, errors);
    if (personal != NULL) {
        prepared->personal_directory = platform_absolute_path(personal, errors);
    }
    return prepared->data_directory != NULL && (personal == NULL || prepared->personal_directory != NULL);
}

/** Exit code IMPRINT_EXIT_OK when the setup is ready for its hooks, else the code to return. */
static int prepare(const char *root, const char *name, const char *personal, prepared_setup *prepared,
                   error_list *errors) {
    prepared_init(prepared);
    if (!workspace_open(root, &prepared->opened, errors)) {
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    if (!resolve_setup(name, prepared, errors)) {
        return IMPRINT_EXIT_INVALID;
    }
    return resolve_paths(personal, prepared, errors) ? IMPRINT_EXIT_OK : IMPRINT_EXIT_ENVIRONMENT;
}

static int refuse_name(const char *name, cli_context *context) {
    fprintf(context->error_output, "imprint: '%s' is not a setup name: %s\n", name, SETUP_NAME_RULE);
    return IMPRINT_EXIT_USAGE;
}

static int run_with_setup_file(prepared_setup *prepared, hook_stage stage, cli_context *context, error_list *errors) {
    char *setup_file = platform_temporary_path(errors);
    if (setup_file == NULL || !file_write_text(setup_file, prepared->json, errors)) {
        free(setup_file);
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    hook_environment environment = { .setup_name = prepared->resolved.name, .setup_file = setup_file,
                                     .data_directory = prepared->data_directory,
                                     .personal_directory = prepared->personal_directory, .log = context->output };
    int status = hooks_run_stage(&prepared->hooks, stage, &environment, errors);
    remove(setup_file);
    free(setup_file);
    return status < 0 ? IMPRINT_EXIT_ENVIRONMENT : status;
}

int command_hooks_run(const char *root, const char *stage_name, const char *name, const char *personal,
                      cli_context *context) {
    hook_stage stage = HOOK_STAGE_BUILD;
    if (!hook_stage_parse(stage_name, &stage)) {
        fprintf(context->error_output, "imprint: unknown hook stage '%s': %s\n", stage_name, HOOK_STAGE_RULE);
        return IMPRINT_EXIT_USAGE;
    }
    if (!setup_name_is_valid(name)) {
        return refuse_name(name, context);
    }
    error_list errors;
    error_list_init(&errors);
    prepared_setup prepared;
    int exit_code = prepare(root, name, personal, &prepared, &errors);
    if (exit_code == IMPRINT_EXIT_OK) {
        exit_code = run_with_setup_file(&prepared, stage, context, &errors);
    }
    prepared_free(&prepared);
    return command_finish(context, &errors, exit_code);
}

int command_hooks_install_firstboot(const char *root, const char *name, const char *target, const char *personal,
                                    cli_context *context) {
    if (!setup_name_is_valid(name)) {
        return refuse_name(name, context);
    }
    error_list errors;
    error_list_init(&errors);
    prepared_setup prepared;
    int exit_code = prepare(root, name, personal, &prepared, &errors);
    if (exit_code == IMPRINT_EXIT_OK) {
        firstboot_request request = { .target_root = target, .setup_name = prepared.resolved.name,
                                      .setup_json = prepared.json, .hooks = &prepared.hooks,
                                      .data_directory = prepared.data_directory,
                                      .personal_directory = prepared.personal_directory };
        bool installed = false;
        bool succeeded = firstboot_install(&request, &installed, &errors);
        firstboot_report(prepared.resolved.name, target, installed, succeeded, context->output);
        exit_code = succeeded ? IMPRINT_EXIT_OK : IMPRINT_EXIT_ENVIRONMENT;
    }
    prepared_free(&prepared);
    return command_finish(context, &errors, exit_code);
}

static bool write_bundle(const prepared_setup *prepared, const char *directory, const char *engine,
                         error_list *errors) {
    char *setup_name = text_format("%s.json", prepared->resolved.name);
    char *setup_file = path_join(directory, setup_name);
    char *data_copy = path_join(directory, "data");
    char *engine_copy = path_join(directory, "imprint-engine");
    bool written = directory_make_all(directory, errors) && file_write_text(setup_file, prepared->json, errors) &&
                   directory_make_all(data_copy, errors) && file_tree_copy(prepared->data_directory, data_copy, errors) &&
                   (engine == NULL || (file_copy(engine, engine_copy, errors) &&
                                       platform_set_mode(engine_copy, 0755, errors)));
    free(setup_name);
    free(setup_file);
    free(data_copy);
    free(engine_copy);
    return written;
}

int command_bundle(const char *root, const char *name, const char *directory, const char *engine,
                   cli_context *context) {
    if (!setup_name_is_valid(name)) {
        return refuse_name(name, context);
    }
    error_list errors;
    error_list_init(&errors);
    prepared_setup prepared;
    int exit_code = prepare(root, name, NULL, &prepared, &errors);
    if (exit_code == IMPRINT_EXIT_OK) {
        exit_code = write_bundle(&prepared, directory, engine, &errors) ? IMPRINT_EXIT_OK : IMPRINT_EXIT_ENVIRONMENT;
    }
    if (exit_code == IMPRINT_EXIT_OK) {
        fprintf(context->output, "wrote %s/%s.json, %s/data%s: copy %s to <stick>/imprint/\n", directory, name, directory,
                engine == NULL ? "" : " and imprint-engine", directory);
    }
    prepared_free(&prepared);
    return command_finish(context, &errors, exit_code);
}
