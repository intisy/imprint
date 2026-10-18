#include "engine.h"

#include "document.h"
#include "exit_codes.h"
#include "plan.h"
#include "resolve.h"

#include <stdlib.h>
#include <string.h>

static const char ENGINE_USAGE[] =
    "usage: imprint-engine plan <resolved-setup.json>\n"
    "       imprint-engine --version\n";

static int plan_file(const char *path, FILE *output, FILE *error_output) {
    error_list errors;
    error_list_init(&errors);
    char *text = NULL;
    if (!file_read_all(path, &text, &errors)) {
        error_list_print(&errors, error_output);
        error_list_free(&errors);
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    resolved_setup resolved;
    bool valid = resolved_setup_from_json(text, path, &resolved, &errors);
    free(text);
    if (valid) {
        plan_print(&resolved, output);
    }
    error_list_print(&errors, error_output);
    error_list_free(&errors);
    resolved_setup_free(&resolved);
    return valid ? IMPRINT_EXIT_OK : IMPRINT_EXIT_INVALID;
}

int engine_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output) {
    if (argument_count == 2 && strcmp(arguments[1], "--version") == 0) {
        fprintf(output, "imprint-engine %s\n", IMPRINT_VERSION);
        return IMPRINT_EXIT_OK;
    }
    if (argument_count != 3 || strcmp(arguments[1], "plan") != 0) {
        fputs(ENGINE_USAGE, error_output);
        return IMPRINT_EXIT_USAGE;
    }
    return plan_file(arguments[2], output, error_output);
}
