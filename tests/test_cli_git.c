#include "cli_harness.h"
#include "exit_codes.h"
#include "string_list.h"
#include "text.h"
#include "utest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROOT IMPRINT_TEST_FIXTURES

typedef struct fake_git {
    string_list commands;
    const char *const *outputs;
    size_t output_count;
    size_t failing_call;
} fake_git;

static int fake_run(const char *const *arguments, char **output, error_list *errors, void *state) {
    (void)errors;
    fake_git *fake = state;
    string_list words;
    string_list_init(&words);
    for (size_t index = 0; arguments[index] != NULL; index++) {
        string_list_add(&words, arguments[index]);
    }
    char *command = string_list_join(&words, " ");
    string_list_add(&fake->commands, command);
    free(command);
    string_list_free(&words);
    size_t call = fake->commands.count - 1;
    const char *answer = call < fake->output_count && fake->outputs[call] != NULL ? fake->outputs[call] : "";
    *output = text_duplicate(answer);
    return call == fake->failing_call ? 1 : 0;
}

static fake_git fake_with(const char *const *outputs, size_t output_count, size_t failing_call) {
    fake_git fake;
    string_list_init(&fake.commands);
    fake.outputs = outputs;
    fake.output_count = output_count;
    fake.failing_call = failing_call;
    return fake;
}

static cli_result run(const char *const *arguments, size_t count, fake_git *fake) {
    return cli_harness_run(arguments, count, NULL, fake_run, fake);
}

#define ASSERT_SOURCE_REFUSED(source)                                                                    \
    do {                                                                                                 \
        fake_git fake = fake_with(NULL, 0, SIZE_MAX);                                                    \
        const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use", (source) };          \
        cli_result result = run(arguments, COUNT_OF(arguments), &fake);                                  \
        ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);                                                 \
        ASSERT_EQ((size_t)0, fake.commands.count);                                                       \
        ASSERT_TRUE(strstr(result.error_output, "usage:") != NULL);                                      \
        cli_result_free(&result);                                                                        \
        string_list_free(&fake.commands);                                                                \
    } while (0)

UTEST(cli_git, data_use_points_the_submodule_at_another_repository) {
    const char *const outputs[] = { "git version 2.47.0\n", "data/\n", "", "", "", "", "", "0123456789abcdef\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use", "intisy/imprint-data" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_EQ((size_t)8, fake.commands.count);
    ASSERT_STREQ("git -C " ROOT "/data rev-parse --show-prefix", fake.commands.items[1]);
    ASSERT_STREQ("git -C " ROOT " submodule update --init -- data", fake.commands.items[2]);
    ASSERT_STREQ("git -C " ROOT " config submodule.data.url https://github.com/intisy/imprint-data.git",
                 fake.commands.items[3]);
    ASSERT_STREQ("git -C " ROOT "/data remote set-url origin https://github.com/intisy/imprint-data.git",
                 fake.commands.items[4]);
    ASSERT_STREQ("git -C " ROOT "/data fetch -q origin HEAD", fake.commands.items[5]);
    ASSERT_STREQ("git -C " ROOT "/data checkout -q FETCH_HEAD", fake.commands.items[6]);
    ASSERT_TRUE(strstr(result.output, "data now at https://github.com/intisy/imprint-data.git @ HEAD (0123456)") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, a_checked_out_data_repo_is_not_reset_to_the_recorded_commit) {
    const char *const outputs[] = { "git version 2.47.0\n", "", "", "", "", "", "0123456789abcdef\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use", "intisy/imprint-data" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_EQ((size_t)7, fake.commands.count);
    ASSERT_TRUE(strstr(fake.commands.items[2], "config submodule.data.url") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, a_reference_after_the_at_sign_is_fetched) {
    const char *const outputs[] = { "git version 2.47.0\n", "" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use", "intisy/imprint-data@v1" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_STREQ("git -C " ROOT "/data fetch -q origin v1", fake.commands.items[4]);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, without_a_source_the_template_comes_back) {
    const char *const outputs[] = { "git version 2.47.0\n", "https://github.com/intisy/imprint-data-template.git\n", "" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_STREQ("git -C " ROOT " config -f .gitmodules submodule.data.url", fake.commands.items[1]);
    ASSERT_STREQ("git -C " ROOT " config submodule.data.url https://github.com/intisy/imprint-data-template.git",
                 fake.commands.items[3]);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, a_source_that_is_no_repository_is_a_usage_error) {
    ASSERT_SOURCE_REFUSED("not a repo");
}

UTEST(cli_git, a_source_starting_with_a_dash_in_the_owner_never_reaches_git) {
    ASSERT_SOURCE_REFUSED("-x/repo");
}

UTEST(cli_git, a_source_starting_with_a_dash_in_the_repository_never_reaches_git) {
    ASSERT_SOURCE_REFUSED("owner/-repo");
}

UTEST(cli_git, a_git_option_as_the_source_never_reaches_git) {
    ASSERT_SOURCE_REFUSED("--upload-pack=x");
}

UTEST(cli_git, an_option_hidden_in_a_url_never_reaches_git) {
    ASSERT_SOURCE_REFUSED("--upload-pack=ssh://host/repo");
}

UTEST(cli_git, a_source_with_a_second_slash_is_refused) {
    ASSERT_SOURCE_REFUSED("owner/repo/extra");
}

UTEST(cli_git, a_source_with_a_space_in_the_repository_is_refused) {
    ASSERT_SOURCE_REFUSED("owner/re po");
}

UTEST(cli_git, a_source_with_a_dot_leading_part_is_refused) {
    ASSERT_SOURCE_REFUSED("owner/.repo");
}

UTEST(cli_git, a_reference_starting_with_a_dash_is_refused) {
    ASSERT_SOURCE_REFUSED("owner/repo@--upload-pack=x");
}

UTEST(cli_git, a_failing_fetch_stops_with_exit_3) {
    const char *const outputs[] = { "git version 2.47.0\n", "data/\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), 5);
    const char *const arguments[] = { "imprint", "--root", ROOT, "data", "use", "intisy/imprint-data" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_EQ((size_t)6, fake.commands.count);
    ASSERT_TRUE(strstr(result.error_output, "fetch -q origin HEAD' failed with exit code 1") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, git_missing_is_exit_3) {
    fake_git fake = fake_with(NULL, 0, 0);
    const char *const arguments[] = { "imprint", "--root", ROOT, "catalog", "update" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "git is not on PATH") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, catalog_update_moves_the_pin_and_says_how_to_commit_it) {
    const char *const outputs[] = { "git version 2.47.0\n", "", "1111111aaaa\n", "", "", "2222222bbbb\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "catalog", "update" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_STREQ("git -C " ROOT "/catalog fetch -q origin HEAD", fake.commands.items[3]);
    ASSERT_TRUE(strstr(result.output, "The catalog moved from 1111111 to 2222222.") != NULL);
    ASSERT_TRUE(strstr(result.output, "git add catalog") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, catalog_update_at_the_newest_commit_changes_nothing) {
    const char *const outputs[] = { "git version 2.47.0\n", "", "1111111aaaa\n", "", "", "1111111aaaa\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "catalog", "update" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(strstr(result.output, "already at the newest commit (1111111)") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, a_catalog_that_does_not_validate_is_rolled_back) {
    const char *const outputs[] = { "git version 2.47.0\n", "", "1111111aaaa\n", "", "", "2222222bbbb\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT "/broken", "catalog", "update" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_STREQ("git -C " ROOT "/broken/catalog checkout -q 1111111aaaa", fake.commands.items[6]);
    ASSERT_TRUE(strstr(result.error_output, "kept the catalog at 1111111") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}

UTEST(cli_git, a_catalog_that_is_not_checked_out_is_exit_3) {
    const char *const outputs[] = { "git version 2.47.0\n", "catalog/\n" };
    fake_git fake = fake_with(outputs, COUNT_OF(outputs), SIZE_MAX);
    const char *const arguments[] = { "imprint", "--root", ROOT, "catalog", "update" };
    cli_result result = run(arguments, COUNT_OF(arguments), &fake);
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "not checked out") != NULL);
    cli_result_free(&result);
    string_list_free(&fake.commands);
}
