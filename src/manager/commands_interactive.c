#include "commands.h"

#include "exit_codes.h"
#include "setup.h"
#include "text.h"
#include "workspace.h"

static const char *const ACTIONS[] = { "List setups", "Show a setup", "Validate", "Quit" };

enum { ACTION_LIST, ACTION_SHOW, ACTION_VALIDATE, ACTION_QUIT };

static void choose_and_show_setup(const char *root, const string_list *names, cli_context *context) {
    size_t chosen = 0;
    if (names->count == 0) {
        ui_log(context->interface, UI_WARNING, "There are no setups in data/setups.");
    } else if (ui_choose_one(context->interface, "Which setup?", (const char *const *)names->items, names->count, 0,
                             &chosen)) {
        command_setup_show(root, names->items[chosen], false, context);
    }
}

static void show_chosen_setup(const char *root, cli_context *context) {
    error_list errors;
    error_list_init(&errors);
    workspace opened;
    string_list names;
    string_list_init(&names);
    bool usable = workspace_open(root, &opened, &errors);
    if (usable) {
        setup_list(opened.data_directory, &names, &errors);
    }
    error_list_print(&errors, context->error_output);
    error_list_free(&errors);
    if (usable) {
        choose_and_show_setup(root, &names, context);
    }
    string_list_free(&names);
    workspace_close(&opened);
}

int command_interactive(const char *root, cli_context *context) {
    if (context->interface == NULL) {
        fputs(cli_usage(), context->error_output);
        return IMPRINT_EXIT_USAGE;
    }
    size_t chosen = 0;
    while (ui_menu(context->interface, "imprint", ACTIONS, COUNT_OF(ACTIONS), &chosen) && chosen != ACTION_QUIT) {
        if (chosen == ACTION_LIST) {
            command_setups(root, context);
        } else if (chosen == ACTION_SHOW) {
            show_chosen_setup(root, context);
        } else {
            command_validate(root, context);
        }
    }
    return IMPRINT_EXIT_OK;
}
