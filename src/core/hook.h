#ifndef IMPRINT_HOOK_H
#define IMPRINT_HOOK_H

#include "document.h"

#include <stdio.h>

typedef enum hook_stage {
    HOOK_STAGE_BUILD,
    HOOK_STAGE_INSTALL,
    HOOK_STAGE_FIRSTBOOT,
    HOOK_STAGE_SESSION
} hook_stage;

typedef struct hook {
    hook_stage stage;
    char *script;
} hook;

typedef struct hook_list {
    hook *items;
    size_t count;
    size_t capacity;
} hook_list;

extern const char HOOK_STAGE_RULE[];

bool hook_stage_parse(const char *name, hook_stage *stage);
const char *hook_stage_name(hook_stage stage);

void hook_list_init(hook_list *hooks);
void hook_list_free(hook_list *hooks);

/** The scripts of one stage, in the order the setup lists them. */
void hook_list_scripts(const hook_list *hooks, hook_stage stage, string_list *scripts);

/** Checks the [[hooks]] of one setup file, including that each script exists in the data repo. */
bool hooks_check_own(const document_node *own, const char *data_directory, error_list *errors);

/** Reads the hooks of a merged setup, where every base's hooks come before its child's. */
bool hooks_read(const document_node *setup, hook_list *hooks, error_list *errors);

/** Every script of the stage exists in the data repo; checked before any of them runs. */
bool hooks_check_scripts(const string_list *scripts, const char *data_directory, error_list *errors);

typedef struct hook_environment {
    const char *setup_name;
    const char *setup_file;
    const char *data_directory;
    const char *personal_directory;
    FILE *log;
} hook_environment;

/**
 * Runs the stage's scripts in order with sh, each with IMPRINT_SETUP, IMPRINT_PERSONAL, IMPRINT_DATA and
 * IMPRINT_STAGE set, and stops at the first that fails. Returns 0, the failed script's exit status, or -1
 * with an error when a script cannot run at all.
 */
int hooks_run_stage(const hook_list *hooks, hook_stage stage, const hook_environment *environment, error_list *errors);

#endif
