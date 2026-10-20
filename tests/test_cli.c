#include "cli_harness.h"
#include "document.h"
#include "exit_codes.h"
#include "text.h"
#include "utest.h"

#include <string.h>

#define ROOT IMPRINT_TEST_FIXTURES
#define SUBMODULE_HINT "git submodule update --init"

static cli_result run(const char *const *arguments, size_t count) {
    return cli_harness_run(arguments, count, NULL, cli_harness_refusing_runner, NULL);
}

static bool contains(const char *text, const char *fragment) {
    return strstr(text, fragment) != NULL;
}

static size_t occurrences(const char *text, const char *fragment) {
    size_t found = 0;
    for (const char *at = strstr(text, fragment); at != NULL; at = strstr(at + strlen(fragment), fragment)) {
        found++;
    }
    return found;
}

static bool reports_not_checked_out(const char *root, const char *command_word, const char *extra_word,
                                    bool catalog_missing, bool data_missing) {
    const char *const arguments[] = { "imprint", "--root", root, command_word, extra_word, "workstation" };
    cli_result result = run(arguments, extra_word == NULL ? 4 : 6);
    bool reported = result.exit_code == IMPRINT_EXIT_ENVIRONMENT && result.output[0] == '\0' &&
                    occurrences(result.error_output, "catalog submodule is not checked out") == (size_t)catalog_missing &&
                    occurrences(result.error_output, "data submodule is not checked out") == (size_t)data_missing &&
                    occurrences(result.error_output, SUBMODULE_HINT) == (size_t)(catalog_missing + data_missing);
    cli_result_free(&result);
    return reported;
}

UTEST(cli, version_prints_the_version) {
    const char *const arguments[] = { "imprint", "--version" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(strncmp(result.output, "imprint ", 8) == 0);
    cli_result_free(&result);
}

UTEST(cli, no_command_without_a_terminal_prints_the_usage) {
    const char *const arguments[] = { "imprint" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "usage: imprint"));
    cli_result_free(&result);
}

UTEST(cli, an_unknown_command_is_a_usage_error) {
    const char *const arguments[] = { "imprint", "frobnicate" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "imprint: unknown command 'frobnicate'"));
    cli_result_free(&result);
}

UTEST(cli, root_needs_a_directory) {
    const char *const arguments[] = { "imprint", "--root" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    cli_result_free(&result);
}

UTEST(cli, setups_lists_the_data_repo) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setups" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_STREQ("base\nexample\nworkstation\n", result.output);
    ASSERT_STREQ("", result.error_output);
    cli_result_free(&result);
}

UTEST(cli, a_fresh_clone_without_submodules_says_how_to_fix_it) {
    const char *const arguments[] = { "imprint", "--root", ROOT "/empty-root", "setups" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, SUBMODULE_HINT));
    ASSERT_STREQ("", result.output);
    cli_result_free(&result);
}

UTEST(cli, every_command_names_both_missing_submodules_once) {
    ASSERT_TRUE(reports_not_checked_out(ROOT "/empty-root", "setups", NULL, true, true));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/empty-root", "validate", NULL, true, true));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/empty-root", "setup", "show", true, true));
}

UTEST(cli, every_command_names_a_missing_catalog_alone) {
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-catalog", "setups", NULL, true, false));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-catalog", "validate", NULL, true, false));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-catalog", "setup", "show", true, false));
}

UTEST(cli, every_command_names_missing_data_alone) {
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-data", "setups", NULL, false, true));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-data", "validate", NULL, false, true));
    ASSERT_TRUE(reports_not_checked_out(ROOT "/missing-data", "setup", "show", false, true));
}

UTEST(cli, a_directory_that_is_no_checkout_is_an_environment_error) {
    const char *const arguments[] = { "imprint", "--root", ROOT "/documents", "setups" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "not an imprint checkout"));
    cli_result_free(&result);
}

UTEST(cli, setups_names_files_that_break_the_pattern_and_lists_the_rest) {
    const char *const arguments[] = { "imprint", "--root", ROOT "/broken", "setups" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(contains(result.output, "loop-a\n"));
    ASSERT_FALSE(contains(result.output, "Workstation"));
    ASSERT_FALSE(contains(result.output, "my setup"));
    ASSERT_TRUE(contains(result.error_output, "'Workstation' is not a setup name"));
    ASSERT_TRUE(contains(result.error_output, "'my setup' is not a setup name"));
    ASSERT_TRUE(contains(result.error_output, "Workstation.toml"));
    ASSERT_TRUE(contains(result.error_output, "my setup.toml"));
    cli_result_free(&result);
}

UTEST(cli, the_setup_listing_is_sorted) {
    const char *const arguments[] = { "imprint", "--root", ROOT "/broken", "setups" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_TRUE(strstr(result.output, "bad-remove\n") < strstr(result.output, "loop-a\n"));
    ASSERT_TRUE(strstr(result.output, "loop-a\n") < strstr(result.output, "workstation-arch\n"));
    cli_result_free(&result);
}

UTEST(cli, setup_show_prints_the_resolved_setup) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setup", "show", "workstation" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "setup: workstation (base -> workstation)\n"));
    ASSERT_TRUE(contains(result.output, "distro: arch (pacman)\n"));
    ASSERT_TRUE(contains(result.output, "window managers: i3\n"));
    ASSERT_TRUE(contains(result.output, "packages (12): git, base-devel, firefox"));
    cli_result_free(&result);
}

UTEST(cli, setup_show_json_is_what_the_engine_reads) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setup", "show", "workstation", "--json" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    error_list errors;
    error_list_init(&errors);
    document_node *parsed = document_parse_json(result.output, "output", &errors);
    ASSERT_TRUE(parsed != NULL);
    ASSERT_EQ(2LL, document_get(parsed, "format")->integer_value);
    ASSERT_STREQ("workstation", document_get(parsed, "name")->string_value);
    document_free(parsed);
    cli_result_free(&result);
}

UTEST(cli, setup_show_of_a_missing_setup_lists_the_available_ones) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setup", "show", "ghost" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "no setup 'ghost'"));
    ASSERT_TRUE(contains(result.error_output, "available setups: base, example, workstation"));
    cli_result_free(&result);
}

UTEST(cli, setup_show_refuses_a_name_that_breaks_the_pattern) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setup", "show", "Bad_Name" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "'Bad_Name' is not a setup name"));
    cli_result_free(&result);
}

UTEST(cli, setup_show_needs_a_name) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "setup", "show" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    cli_result_free(&result);
}

UTEST(cli, validate_reports_no_problems_on_good_data) {
    const char *const arguments[] = { "imprint", "--root", ROOT, "validate" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_STREQ("catalog: apps 10, distros 2, window managers 1, bootloaders 1; data: setups 3; no problems\n",
                 result.output);
    cli_result_free(&result);
}

UTEST(cli, validate_reports_every_problem_of_a_broken_root) {
    const char *const arguments[] = { "imprint", "--root", ROOT "/broken", "validate" };
    cli_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "install.arch.pakages: unknown key"));
    ASSERT_TRUE(contains(result.error_output, "'Workstation' is not a setup name"));
    ASSERT_TRUE(contains(result.error_output, "'my setup' is not a setup name"));
    ASSERT_TRUE(contains(result.error_output, "setup inheritance cycle"));
    ASSERT_TRUE(contains(result.error_output, " problems\n"));
    cli_result_free(&result);
}

UTEST(cli, the_menu_shows_a_chosen_setup) {
    const char *const answers[] = { "2", "3", "4" };
    ui *scripted = ui_scripted_create(answers, COUNT_OF(answers));
    const char *const arguments[] = { "imprint", "--root", ROOT };
    cli_result result = cli_harness_run(arguments, COUNT_OF(arguments), scripted, cli_harness_refusing_runner, NULL);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "setup: workstation"));
    ASSERT_FALSE(ui_scripted_exhausted(scripted));
    ASSERT_TRUE(contains(ui_scripted_transcript(scripted), "choose one: Which setup?\n"));
    ui_destroy(scripted);
    cli_result_free(&result);
}

UTEST(cli, the_menu_on_a_fresh_clone_reports_the_submodules_instead_of_no_setups) {
    const char *const answers[] = { "2", "4" };
    ui *scripted = ui_scripted_create(answers, COUNT_OF(answers));
    const char *const arguments[] = { "imprint", "--root", ROOT "/empty-root" };
    cli_result result = cli_harness_run(arguments, COUNT_OF(arguments), scripted, cli_harness_refusing_runner, NULL);
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, SUBMODULE_HINT));
    ASSERT_FALSE(contains(ui_scripted_transcript(scripted), "no setups"));
    ui_destroy(scripted);
    cli_result_free(&result);
}
