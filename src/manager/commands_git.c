#include "commands.h"

#include "catalog.h"
#include "exit_codes.h"
#include "string_list.h"
#include "text.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { SHORT_COMMIT_LENGTH = 7 };

static const char *const REMOTE_DEFAULT_BRANCH = "HEAD";

typedef struct data_source {
    char *url;
    char *reference;
} data_source;

static char *trimmed(char *text) {
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
    return text;
}

static char *describe(const char *const *arguments) {
    string_list words;
    string_list_init(&words);
    for (size_t index = 0; arguments[index] != NULL; index++) {
        string_list_add(&words, arguments[index]);
    }
    char *joined = string_list_join(&words, " ");
    string_list_free(&words);
    return joined;
}

static bool run_git(cli_context *context, const char *const *arguments, char **output, error_list *errors) {
    char *captured = NULL;
    int status = context->run(arguments, &captured, errors, context->run_state);
    if (status != 0) {
        if (status != -1) {
            char *command = describe(arguments);
            error_push(errors, NULL, 0, "'%s' failed with exit code %d: %s", command, status,
                       captured == NULL ? "" : trimmed(captured));
            free(command);
        }
        free(captured);
        return false;
    }
    if (output != NULL) {
        *output = trimmed(captured == NULL ? text_duplicate("") : captured);
    } else {
        free(captured);
    }
    return true;
}

static bool git_available(cli_context *context, error_list *errors) {
    const char *const arguments[] = { "git", "--version", NULL };
    error_list ignored;
    error_list_init(&ignored);
    bool available = run_git(context, arguments, NULL, &ignored);
    error_list_free(&ignored);
    if (!available) {
        error_push(errors, NULL, 0, "git is not on PATH: install git and run imprint again");
    }
    return available;
}

/**
 * @implNote In a submodule folder that was never checked out, git answers for the superproject, so
 * --show-prefix prints the folder's own path there and nothing inside a real checkout.
 */
static bool is_own_checkout(cli_context *context, const char *directory) {
    const char *const arguments[] = { "git", "-C", directory, "rev-parse", "--show-prefix", NULL };
    char *prefix = NULL;
    error_list ignored;
    error_list_init(&ignored);
    bool own = run_git(context, arguments, &prefix, &ignored) && prefix[0] == '\0';
    error_list_free(&ignored);
    free(prefix);
    return own;
}

static bool is_name_character(unsigned char character, const char *extra_characters) {
    return isalnum(character) || (character != '\0' && strchr(extra_characters, character) != NULL);
}

/** @implNote A part starting with - or . could reach git as an option or a hidden path, so it is refused. */
static bool is_name_text(const char *text, size_t length, const char *extra_characters) {
    if (length == 0 || text[0] == '-' || text[0] == '.') {
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        if (!is_name_character((unsigned char)text[index], extra_characters)) {
            return false;
        }
    }
    return true;
}

static bool is_reference(const char *text) {
    if (text[0] == '\0' || text[0] == '-') {
        return false;
    }
    for (const char *character = text; *character != '\0'; character++) {
        if (!is_name_character((unsigned char)*character, "-_./")) {
            return false;
        }
    }
    return true;
}

static bool is_owner_and_repository(const char *location) {
    const char *slash = strchr(location, '/');
    return slash != NULL && strchr(slash + 1, '/') == NULL &&
           is_name_text(location, (size_t)(slash - location), "-") && is_name_text(slash + 1, strlen(slash + 1), "-_.");
}

/** @implNote A - at the start, or right after the scheme or git@, would be read by git or ssh as an option; only these two forms name a remote. */
static bool is_git_url(const char *text) {
    if (text[0] == '-' || text[0] == '\0') {
        return false;
    }
    for (const char *character = text; *character != '\0'; character++) {
        if (!isgraph((unsigned char)*character)) {
            return false;
        }
    }
    const char *scheme_end = strstr(text, "://");
    if (scheme_end != NULL) {
        return scheme_end[3] != '-';
    }
    return strncmp(text, "git@", 4) == 0 && text[4] != '-';
}

/** @implNote The reference is split at an @ after the last slash, so the @ in git@host:owner/repo stays in the URL. */
static bool parse_data_source(const char *source, data_source *parsed) {
    const char *last_slash = strrchr(source, '/');
    const char *at = last_slash == NULL ? NULL : strchr(last_slash, '@');
    size_t location_length = at == NULL ? strlen(source) : (size_t)(at - source);
    char *location = memory_allocate(location_length + 1);
    memcpy(location, source, location_length);
    location[location_length] = '\0';
    parsed->reference = text_duplicate(at == NULL ? REMOTE_DEFAULT_BRANCH : at + 1);
    bool valid = is_reference(parsed->reference);
    if (strstr(location, "://") != NULL || strncmp(location, "git@", 4) == 0) {
        parsed->url = location;
        return valid && is_git_url(location);
    }
    valid = valid && is_owner_and_repository(location);
    parsed->url = text_format("https://github.com/%s.git", location);
    free(location);
    return valid;
}

static bool template_url(const char *root, cli_context *context, char **url, error_list *errors) {
    const char *const arguments[] = { "git", "-C", root, "config", "-f", ".gitmodules", "submodule.data.url", NULL };
    if (!run_git(context, arguments, url, errors)) {
        return false;
    }
    if (!is_git_url(*url)) {
        error_push(errors, ".gitmodules", 0, "submodule.data.url is not a usable git URL");
        return false;
    }
    return true;
}

static bool initialise_data(const char *root, const char *data_directory, cli_context *context, error_list *errors) {
    if (is_own_checkout(context, data_directory)) {
        return true;
    }
    const char *const arguments[] = { "git", "-C", root, "submodule", "update", "--init", "--", "data", NULL };
    return run_git(context, arguments, NULL, errors);
}

static bool switch_data(const char *root, const char *data_directory, const char *url, const char *reference,
                        cli_context *context, char **commit, error_list *errors) {
    const char *const set_submodule_url[] = { "git", "-C", root, "config", "submodule.data.url", url, NULL };
    const char *const set_remote[] = { "git", "-C", data_directory, "remote", "set-url", "origin", url, NULL };
    const char *const fetch[] = { "git", "-C", data_directory, "fetch", "-q", "origin", reference, NULL };
    const char *const checkout[] = { "git", "-C", data_directory, "checkout", "-q", "FETCH_HEAD", NULL };
    const char *const head[] = { "git", "-C", data_directory, "rev-parse", "HEAD", NULL };
    return run_git(context, set_submodule_url, NULL, errors) && run_git(context, set_remote, NULL, errors) &&
           run_git(context, fetch, NULL, errors) && run_git(context, checkout, NULL, errors) &&
           run_git(context, head, commit, errors);
}

static int point_data_at(const char *root, const data_source *parsed, cli_context *context, error_list *errors) {
    if (!git_available(context, errors)) {
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    char *data_directory = path_join(root, "data");
    char *url = NULL;
    char *commit = NULL;
    const char *reference = parsed == NULL ? REMOTE_DEFAULT_BRANCH : parsed->reference;
    bool done = true;
    if (parsed == NULL) {
        done = template_url(root, context, &url, errors);
    } else {
        url = text_duplicate(parsed->url);
    }
    done = done && initialise_data(root, data_directory, context, errors) &&
           switch_data(root, data_directory, url, reference, context, &commit, errors);
    if (done) {
        fprintf(context->output, "data now at %s @ %s (%.*s)\n", url, reference, SHORT_COMMIT_LENGTH, commit);
        fputs("The data pointer stays uncommitted in imprint: commit inside data/ and push that repository.\n",
              context->output);
    }
    free(commit);
    free(url);
    free(data_directory);
    return done ? IMPRINT_EXIT_OK : IMPRINT_EXIT_ENVIRONMENT;
}

int command_data_use(const char *root, const char *source, cli_context *context) {
    data_source parsed = { NULL, NULL };
    if (source != NULL && !parse_data_source(source, &parsed)) {
        fprintf(context->error_output, "imprint: '%s' is not owner/repo[@ref] or a git URL\n\n%s", source,
                cli_usage());
        free(parsed.url);
        free(parsed.reference);
        return IMPRINT_EXIT_USAGE;
    }
    error_list errors;
    error_list_init(&errors);
    int exit_code = point_data_at(root, source == NULL ? NULL : &parsed, context, &errors);
    free(parsed.url);
    free(parsed.reference);
    return command_finish(context, &errors, exit_code);
}

static int check_new_catalog(const char *catalog_directory, const char *data_directory, const char *previous,
                             const char *current, cli_context *context, error_list *errors) {
    if (strcmp(previous, current) == 0) {
        fprintf(context->output, "The catalog is already at the newest commit (%.*s).\n", SHORT_COMMIT_LENGTH, current);
        return IMPRINT_EXIT_OK;
    }
    catalog available;
    bool valid = catalog_load(catalog_directory, data_directory, &available, errors);
    catalog_free(&available);
    if (!valid) {
        const char *const back[] = { "git", "-C", catalog_directory, "checkout", "-q", previous, NULL };
        if (!run_git(context, back, NULL, errors)) {
            error_push(errors, NULL, 0,
                       "the catalog is still at %.*s and does not validate with your data: run git -C %s checkout %s",
                       SHORT_COMMIT_LENGTH, current, catalog_directory, previous);
            return IMPRINT_EXIT_ENVIRONMENT;
        }
        error_push(errors, NULL, 0, "kept the catalog at %.*s: the newest catalog does not validate with your data",
                   SHORT_COMMIT_LENGTH, previous);
        return IMPRINT_EXIT_INVALID;
    }
    fprintf(context->output,
            "The catalog moved from %.*s to %.*s.\n"
            "Commit the new pin in imprint: git add catalog && git commit -m \"chore(catalog): move the catalog pin\"\n",
            SHORT_COMMIT_LENGTH, previous, SHORT_COMMIT_LENGTH, current);
    return IMPRINT_EXIT_OK;
}

static int move_catalog_pin(const char *root, cli_context *context, error_list *errors) {
    if (!git_available(context, errors)) {
        return IMPRINT_EXIT_ENVIRONMENT;
    }
    char *catalog_directory = path_join(root, "catalog");
    char *data_directory = path_join(root, "data");
    char *previous = NULL;
    char *current = NULL;
    int exit_code = IMPRINT_EXIT_ENVIRONMENT;
    const char *const head[] = { "git", "-C", catalog_directory, "rev-parse", "HEAD", NULL };
    const char *const fetch[] = { "git", "-C", catalog_directory, "fetch", "-q", "origin", REMOTE_DEFAULT_BRANCH, NULL };
    const char *const checkout[] = { "git", "-C", catalog_directory, "checkout", "-q", "FETCH_HEAD", NULL };
    if (!is_own_checkout(context, catalog_directory)) {
        error_push(errors, catalog_directory, 0, "not checked out (run: git submodule update --init)");
    } else if (run_git(context, head, &previous, errors) && run_git(context, fetch, NULL, errors) &&
               run_git(context, checkout, NULL, errors) && run_git(context, head, &current, errors)) {
        exit_code = check_new_catalog(catalog_directory, data_directory, previous, current, context, errors);
    }
    free(previous);
    free(current);
    free(catalog_directory);
    free(data_directory);
    return exit_code;
}

int command_catalog_update(const char *root, cli_context *context) {
    error_list errors;
    error_list_init(&errors);
    int exit_code = move_catalog_pin(root, context, &errors);
    return command_finish(context, &errors, exit_code);
}
