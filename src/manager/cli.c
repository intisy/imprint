#include "cli.h"

#include "commands.h"
#include "exit_codes.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

static const char USAGE[] =
    "usage: imprint [--root DIR] <command>\n"
    "\n"
    "commands:\n"
    "  setups                        list the setups in data/setups\n"
    "  setup show <name> [--json]    a setup with everything it inherits, resolved against the catalog\n"
    "  validate                      check the catalog and every setup\n"
    "  data use [owner/repo[@ref]]   point data/ at a data repo; without one, back to the template\n"
    "  catalog update                move the catalog pin to the newest imprint-catalog commit\n"
    "\n"
    "DIR defaults to IMPRINT_ROOT, else the current directory. Without a command, imprint opens a menu.\n";

typedef struct invocation {
    const char *root;
    const char *const *words;
    size_t word_count;
} invocation;

const char *cli_usage(void) {
    return USAGE;
}

int cli_platform_runner(const char *const *arguments, char **output, error_list *errors, void *state) {
    (void)state;
    return platform_run_capture(arguments, output, errors);
}

static int usage_error(cli_context *context, const char *problem, const char *word) {
    fprintf(context->error_output, "imprint: %s '%s'\n\n%s", problem, word, USAGE);
    return IMPRINT_EXIT_USAGE;
}

static bool parse_options(int count, const char *const *arguments, invocation *parsed, cli_context *context,
                          int *exit_code) {
    const char *environment_root = getenv("IMPRINT_ROOT");
    parsed->root = environment_root != NULL && environment_root[0] != '\0' ? environment_root : ".";
    int index = 1;
    while (index < count && strncmp(arguments[index], "--", 2) == 0) {
        if (strcmp(arguments[index], "--root") == 0 && index + 1 < count) {
            parsed->root = arguments[index + 1];
            index += 2;
        } else if (strcmp(arguments[index], "--root") == 0) {
            *exit_code = usage_error(context, "a directory must follow", "--root");
            return false;
        } else if (strcmp(arguments[index], "--version") == 0) {
            fprintf(context->output, "imprint %s\n", IMPRINT_VERSION);
            *exit_code = IMPRINT_EXIT_OK;
            return false;
        } else if (strcmp(arguments[index], "--help") == 0) {
            fputs(USAGE, context->output);
            *exit_code = IMPRINT_EXIT_OK;
            return false;
        } else {
            *exit_code = usage_error(context, "unknown option", arguments[index]);
            return false;
        }
    }
    parsed->words = arguments + index;
    parsed->word_count = (size_t)(count - index);
    return true;
}

static bool word_is(const invocation *parsed, size_t index, const char *word) {
    return parsed->word_count > index && strcmp(parsed->words[index], word) == 0;
}

static int run_setup(const invocation *parsed, cli_context *context) {
    if (!word_is(parsed, 1, "show")) {
        return usage_error(context, "unknown setup command", parsed->word_count > 1 ? parsed->words[1] : "");
    }
    if (parsed->word_count < 3) {
        return usage_error(context, "a setup name must follow", "setup show");
    }
    bool as_json = false;
    for (size_t index = 3; index < parsed->word_count; index++) {
        if (strcmp(parsed->words[index], "--json") != 0) {
            return usage_error(context, "unknown argument", parsed->words[index]);
        }
        as_json = true;
    }
    return command_setup_show(parsed->root, parsed->words[2], as_json, context);
}

static int run_data(const invocation *parsed, cli_context *context) {
    if (!word_is(parsed, 1, "use")) {
        return usage_error(context, "unknown data command", parsed->word_count > 1 ? parsed->words[1] : "");
    }
    if (parsed->word_count > 3) {
        return usage_error(context, "too many arguments for", "data use");
    }
    return command_data_use(parsed->root, parsed->word_count == 3 ? parsed->words[2] : NULL, context);
}

static int run_catalog(const invocation *parsed, cli_context *context) {
    if (!word_is(parsed, 1, "update") || parsed->word_count != 2) {
        return usage_error(context, "unknown catalog command", parsed->word_count > 1 ? parsed->words[1] : "");
    }
    return command_catalog_update(parsed->root, context);
}

static int run_alone(const invocation *parsed, cli_context *context, int (*command)(const char *, cli_context *)) {
    if (parsed->word_count > 1) {
        return usage_error(context, "too many arguments for", parsed->words[0]);
    }
    return command(parsed->root, context);
}

int cli_run(int argument_count, const char *const *arguments, cli_context *context) {
    invocation parsed;
    int exit_code = IMPRINT_EXIT_OK;
    if (!parse_options(argument_count, arguments, &parsed, context, &exit_code)) {
        return exit_code;
    }
    if (parsed.word_count == 0) {
        return command_interactive(parsed.root, context);
    }
    const char *command = parsed.words[0];
    if (strcmp(command, "setups") == 0) {
        return run_alone(&parsed, context, command_setups);
    }
    if (strcmp(command, "validate") == 0) {
        return run_alone(&parsed, context, command_validate);
    }
    if (strcmp(command, "setup") == 0) {
        return run_setup(&parsed, context);
    }
    if (strcmp(command, "data") == 0) {
        return run_data(&parsed, context);
    }
    if (strcmp(command, "catalog") == 0) {
        return run_catalog(&parsed, context);
    }
    if (strcmp(command, "help") == 0) {
        fputs(USAGE, context->output);
        return IMPRINT_EXIT_OK;
    }
    return usage_error(context, "unknown command", command);
}
