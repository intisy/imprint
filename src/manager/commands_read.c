#include "commands.h"

#include "catalog.h"
#include "exit_codes.h"
#include "identifier.h"
#include "platform.h"
#include "resolve.h"
#include "setup.h"
#include "text.h"
#include "workspace.h"

#include <stdlib.h>
#include <string.h>

int command_finish(cli_context *context, error_list *errors, int exit_code) {
    error_list_print(errors, context->error_output);
    error_list_free(errors);
    return exit_code;
}

int command_setups(const char *root, cli_context *context) {
    error_list errors;
    error_list_init(&errors);
    workspace opened;
    if (!workspace_open(root, &opened, &errors)) {
        workspace_close(&opened);
        return command_finish(context, &errors, IMPRINT_EXIT_ENVIRONMENT);
    }
    string_list names;
    string_list_init(&names);
    bool valid = setup_list(opened.data_directory, &names, &errors);
    for (size_t index = 0; index < names.count; index++) {
        fprintf(context->output, "%s\n", names.items[index]);
    }
    string_list_free(&names);
    workspace_close(&opened);
    return command_finish(context, &errors, valid ? IMPRINT_EXIT_OK : IMPRINT_EXIT_INVALID);
}

static void print_list(FILE *output, const char *label, const string_list *values) {
    char *joined = string_list_join(values, ", ");
    fprintf(output, "%s (%zu)%s%s\n", label, values->count, values->count > 0 ? ": " : "", joined);
    free(joined);
}

static void print_summary(const setup *loaded, const resolved_setup *resolved, FILE *output) {
    if (loaded->chain.count > 1) {
        char *chain = string_list_join(&loaded->chain, " -> ");
        fprintf(output, "setup: %s (%s)\n", resolved->name, chain);
        free(chain);
    } else {
        fprintf(output, "setup: %s\n", resolved->name);
    }
    fprintf(output, "distro: %s (%s)\n", resolved->distro, resolved->package_manager);
    fprintf(output, "bootloader: %s\n", resolved->bootloader);
    char *window_managers = string_list_join(&resolved->window_managers, ", ");
    fprintf(output, "window managers: %s\n", window_managers[0] == '\0' ? "none" : window_managers);
    free(window_managers);
    print_list(output, "apps", &resolved->apps);
    print_list(output, "packages", &resolved->packages);
    print_list(output, "aur", &resolved->aur);
    print_list(output, "flatpak", &resolved->flatpak);
    print_list(output, "services", &resolved->services);
    print_list(output, "user services", &resolved->user_services);
    if (resolved->skipped.count > 0) {
        char *label = text_format("skipped, no recipe for %s", resolved->distro);
        print_list(output, label, &resolved->skipped);
        free(label);
    }
}

static void suggest_setups(const char *data_directory, cli_context *context) {
    string_list names;
    string_list_init(&names);
    error_list ignored;
    error_list_init(&ignored);
    setup_list(data_directory, &names, &ignored);
    char *joined = string_list_join(&names, ", ");
    fprintf(context->error_output, "available setups: %s\n", names.count == 0 ? "none" : joined);
    free(joined);
    string_list_free(&names);
    error_list_free(&ignored);
}

static void print_resolved(const setup *loaded, const resolved_setup *resolved, bool as_json, cli_context *context) {
    if (as_json) {
        char *json = resolved_setup_to_json(resolved);
        fputs(json, context->output);
        free(json);
    } else {
        print_summary(loaded, resolved, context->output);
    }
}

int command_setup_show(const char *root, const char *name, bool as_json, cli_context *context) {
    if (!setup_name_is_valid(name)) {
        fprintf(context->error_output, "imprint: '%s' is not a setup name: %s\n", name, SETUP_NAME_RULE);
        return IMPRINT_EXIT_USAGE;
    }
    error_list errors;
    error_list_init(&errors);
    workspace opened;
    if (!workspace_open(root, &opened, &errors)) {
        workspace_close(&opened);
        return command_finish(context, &errors, IMPRINT_EXIT_ENVIRONMENT);
    }
    catalog available;
    setup loaded;
    resolved_setup resolved;
    memset(&loaded, 0, sizeof loaded);
    resolved_setup_init(&resolved);
    int exit_code = IMPRINT_EXIT_INVALID;
    bool catalog_valid = catalog_load(opened.catalog_directory, opened.data_directory, &available, &errors);
    if (catalog_valid && setup_load(opened.data_directory, name, &loaded, &errors) &&
        setup_resolve(&loaded, &available, &resolved, &errors)) {
        print_resolved(&loaded, &resolved, as_json, context);
        exit_code = IMPRINT_EXIT_OK;
    }
    error_list_print(&errors, context->error_output);
    if (catalog_valid && !setup_file_exists(opened.data_directory, name)) {
        suggest_setups(opened.data_directory, context);
    }
    error_list_free(&errors);
    resolved_setup_free(&resolved);
    setup_free(&loaded);
    catalog_free(&available);
    workspace_close(&opened);
    return exit_code;
}

static bool check_one_setup(const char *data_directory, const char *name, const catalog *available, bool resolve,
                            error_list *errors) {
    setup loaded;
    resolved_setup resolved;
    resolved_setup_init(&resolved);
    bool valid = setup_load(data_directory, name, &loaded, errors);
    if (valid && resolve) {
        valid = setup_resolve(&loaded, available, &resolved, errors);
    }
    resolved_setup_free(&resolved);
    setup_free(&loaded);
    return valid;
}

int command_validate(const char *root, cli_context *context) {
    error_list errors;
    error_list_init(&errors);
    workspace opened;
    if (!workspace_open(root, &opened, &errors)) {
        workspace_close(&opened);
        return command_finish(context, &errors, IMPRINT_EXIT_ENVIRONMENT);
    }
    catalog available;
    bool catalog_valid = catalog_load(opened.catalog_directory, opened.data_directory, &available, &errors);
    string_list names;
    string_list_init(&names);
    bool valid = setup_list(opened.data_directory, &names, &errors) && catalog_valid;
    for (size_t index = 0; index < names.count; index++) {
        valid = check_one_setup(opened.data_directory, names.items[index], &available, catalog_valid, &errors) && valid;
    }
    if (valid) {
        fprintf(context->output,
                "catalog: apps %zu, distros %zu, window managers %zu, bootloaders %zu; data: setups %zu; no problems\n",
                available.apps.count, available.distros.count, available.window_managers.count,
                available.bootloaders.count, names.count);
    }
    error_list_print(&errors, context->error_output);
    if (!valid) {
        fprintf(context->error_output, "%zu problems\n", errors.count);
    }
    error_list_free(&errors);
    string_list_free(&names);
    catalog_free(&available);
    workspace_close(&opened);
    return valid ? IMPRINT_EXIT_OK : IMPRINT_EXIT_INVALID;
}
