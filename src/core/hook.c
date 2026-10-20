#include "hook.h"

#include "platform.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

static const char *const STAGE_NAMES[] = { "build", "install", "firstboot", "session" };
static const char *const HOOK_KEYS[] = { "stage", "script" };

const char HOOK_STAGE_RULE[] = "one of build, install, firstboot, session";

bool hook_stage_parse(const char *name, hook_stage *stage) {
    for (size_t index = 0; index < COUNT_OF(STAGE_NAMES); index++) {
        if (strcmp(name, STAGE_NAMES[index]) == 0) {
            *stage = (hook_stage)index;
            return true;
        }
    }
    return false;
}

const char *hook_stage_name(hook_stage stage) {
    return STAGE_NAMES[stage];
}

void hook_list_init(hook_list *hooks) {
    memset(hooks, 0, sizeof *hooks);
}

void hook_list_free(hook_list *hooks) {
    for (size_t index = 0; index < hooks->count; index++) {
        free(hooks->items[index].script);
    }
    free(hooks->items);
    hook_list_init(hooks);
}

static void hook_list_add(hook_list *hooks, hook_stage stage, const char *script) {
    if (hooks->count == hooks->capacity) {
        hooks->capacity = hooks->capacity == 0 ? 4 : hooks->capacity * 2;
        hooks->items = memory_reallocate(hooks->items, hooks->capacity * sizeof *hooks->items);
    }
    hooks->items[hooks->count].stage = stage;
    hooks->items[hooks->count].script = text_duplicate(script);
    hooks->count++;
}

void hook_list_scripts(const hook_list *hooks, hook_stage stage, string_list *scripts) {
    for (size_t index = 0; index < hooks->count; index++) {
        if (hooks->items[index].stage == stage) {
            string_list_add(scripts, hooks->items[index].script);
        }
    }
}

static bool is_script_character(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || strchr("._-/", character) != NULL;
}

static bool has_only_script_characters(const char *script) {
    for (const char *character = script; *character != '\0'; character++) {
        if (!is_script_character(*character)) {
            return false;
        }
    }
    return true;
}

static bool is_plain_segment(const char *segment, size_t length) {
    return length > 0 && !(length == 1 && segment[0] == '.') && !(length == 2 && strncmp(segment, "..", 2) == 0);
}

static bool stays_inside_the_repo(const char *script) {
    const char *segment = script;
    while (true) {
        const char *end = strchr(segment, '/');
        size_t length = end == NULL ? strlen(segment) : (size_t)(end - segment);
        if (!is_plain_segment(segment, length)) {
            return false;
        }
        if (end == NULL) {
            return true;
        }
        segment = end + 1;
    }
}

static bool check_script_path(const document_node *node, const char *script, error_list *errors) {
    if (!has_only_script_characters(script)) {
        document_error(errors, node, "hook script '%s' may hold only letters, digits, '.', '_', '-' and '/'", script);
        return false;
    }
    if (!stays_inside_the_repo(script)) {
        document_error(errors, node, "hook script '%s' leaves the data repo: give a path inside it, without '..', "
                                     "'.' or a leading '/'", script);
        return false;
    }
    return true;
}

static bool read_entry(const document_node *entry, hook_stage *stage, const char **script, error_list *errors) {
    if (entry->kind != DOCUMENT_TABLE) {
        document_error(errors, entry, "must be a table with a stage and a script, found %s", document_kind_name(entry->kind));
        return false;
    }
    bool valid = document_check_keys(entry, HOOK_KEYS, COUNT_OF(HOOK_KEYS), errors);
    const char *stage_name = NULL;
    if (!document_require_string(entry, "stage", &stage_name, errors)) {
        valid = false;
    } else if (!hook_stage_parse(stage_name, stage)) {
        document_error(errors, document_get(entry, "stage"), "unknown hook stage '%s': %s", stage_name, HOOK_STAGE_RULE);
        valid = false;
    }
    if (document_require_string(entry, "script", script, errors)) {
        valid = check_script_path(document_get(entry, "script"), *script, errors) && valid;
    } else {
        valid = false;
    }
    return valid;
}

static const document_node *hooks_array(const document_node *setup, bool *valid, error_list *errors) {
    const document_node *hooks = document_get(setup, "hooks");
    *valid = hooks == NULL || hooks->kind == DOCUMENT_ARRAY;
    if (!*valid) {
        document_error(errors, hooks, "must be an array of [[hooks]] tables, found %s", document_kind_name(hooks->kind));
    }
    return *valid ? hooks : NULL;
}

static bool script_exists(const document_node *node, const char *script, const char *data_directory, error_list *errors) {
    char *path = path_join(data_directory, script);
    bool exists = platform_is_file(path);
    if (!exists) {
        document_error(errors, node, "hook script '%s' is not in the data repo (%s)", script, path);
    }
    free(path);
    return exists;
}

bool hooks_check_own(const document_node *own, const char *data_directory, error_list *errors) {
    bool valid = true;
    const document_node *hooks = hooks_array(own, &valid, errors);
    for (size_t index = 0; hooks != NULL && index < hooks->child_count; index++) {
        hook_stage stage = HOOK_STAGE_BUILD;
        const char *script = NULL;
        const document_node *entry = hooks->children[index];
        if (read_entry(entry, &stage, &script, errors)) {
            valid = script_exists(document_get(entry, "script"), script, data_directory, errors) && valid;
        } else {
            valid = false;
        }
    }
    return valid;
}

bool hooks_read(const document_node *setup, hook_list *hooks, error_list *errors) {
    hook_list_init(hooks);
    bool valid = true;
    const document_node *entries = hooks_array(setup, &valid, errors);
    for (size_t index = 0; entries != NULL && index < entries->child_count; index++) {
        hook_stage stage = HOOK_STAGE_BUILD;
        const char *script = NULL;
        if (read_entry(entries->children[index], &stage, &script, errors)) {
            hook_list_add(hooks, stage, script);
        } else {
            valid = false;
        }
    }
    return valid;
}

bool hooks_check_scripts(const string_list *scripts, const char *data_directory, error_list *errors) {
    bool valid = true;
    for (size_t index = 0; index < scripts->count; index++) {
        valid = script_exists(NULL, scripts->items[index], data_directory, errors) && valid;
    }
    return valid;
}

static void export_environment(const hook_environment *environment, const char *stage_name) {
    platform_set_environment("IMPRINT_SETUP", environment->setup_file);
    platform_set_environment("IMPRINT_DATA", environment->data_directory);
    platform_set_environment("IMPRINT_STAGE", stage_name);
    const char *personal = environment->personal_directory == NULL ? "" : environment->personal_directory;
    platform_set_environment("IMPRINT_PERSONAL", personal);
    if (personal[0] == '\0') {
        fprintf(environment->log, "imprint: IMPRINT_PERSONAL is empty: no personal folder was given\n");
    }
}

static int run_script(const hook_environment *environment, const char *script, error_list *errors) {
    char *path = path_join(environment->data_directory, script);
    const char *const arguments[] = { "sh", path, NULL };
    int status = platform_run_streaming(arguments, environment->log, errors);
    free(path);
    return status;
}

static int run_scripts(const string_list *scripts, const char *stage_name, const hook_environment *environment,
                       error_list *errors) {
    for (size_t index = 0; index < scripts->count; index++) {
        const char *script = scripts->items[index];
        fprintf(environment->log, "imprint: %s hook %zu of %zu: %s\n", stage_name, index + 1, scripts->count, script);
        int status = run_script(environment, script, errors);
        if (status > 0) {
            fprintf(environment->log, "imprint: %s failed with exit code %d; the %s hooks after it did not run\n",
                    script, status, stage_name);
        }
        if (status != 0) {
            return status;
        }
    }
    fprintf(environment->log, "imprint: every %s hook of setup '%s' succeeded\n", stage_name, environment->setup_name);
    return 0;
}

int hooks_run_stage(const hook_list *hooks, hook_stage stage, const hook_environment *environment, error_list *errors) {
    const char *stage_name = hook_stage_name(stage);
    string_list scripts;
    string_list_init(&scripts);
    hook_list_scripts(hooks, stage, &scripts);
    int status = 0;
    if (scripts.count == 0) {
        fprintf(environment->log, "imprint: setup '%s' has no %s hooks\n", environment->setup_name, stage_name);
    } else if (!hooks_check_scripts(&scripts, environment->data_directory, errors)) {
        status = -1;
    } else {
        export_environment(environment, stage_name);
        status = run_scripts(&scripts, stage_name, environment, errors);
    }
    string_list_free(&scripts);
    return status;
}
