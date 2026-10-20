#include "cli_harness.h"
#include "document.h"
#include "engine.h"
#include "exit_codes.h"
#include "platform.h"
#include "text.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

#define HOOKS_ROOT IMPRINT_TEST_FIXTURES "/hooks"
#define RESOLVED IMPRINT_TEST_FIXTURES "/resolved"

static cli_result run_manager(const char *const *arguments, size_t count) {
    return cli_harness_run(arguments, count, NULL, cli_harness_refusing_runner, NULL);
}

static cli_result run_engine(const char *const *arguments, size_t count) {
    FILE *output = platform_temporary_file();
    FILE *error_output = platform_temporary_file();
    cli_result result;
    result.exit_code = engine_run((int)count, arguments, output, error_output);
    rewind(output);
    rewind(error_output);
    result.output = platform_read_stream(output);
    result.error_output = platform_read_stream(error_output);
    fclose(output);
    fclose(error_output);
    return result;
}

static bool contains(const char *text, const char *fragment) {
    return strstr(text, fragment) != NULL;
}

UTEST(hook_cli, setup_show_lists_the_hooks_base_first) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "setup", "show", "hooked" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "hooks (3): firstboot hooks/first.sh, build hooks/build.sh, firstboot hooks/second.sh\n"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_resolved_json_carries_the_hooks) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "setup", "show", "hooked", "--json" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "\"hooks\""));
    ASSERT_TRUE(contains(result.output, "hooks/second.sh"));
    cli_result_free(&result);
}

UTEST(hook_cli, an_unknown_stage_is_a_usage_error) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "hooks", "run", "boot", "hooked" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "unknown hook stage 'boot': one of build, install, firstboot, session"));
    cli_result_free(&result);
}

UTEST(hook_cli, install_firstboot_needs_a_target) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "hooks", "install-firstboot", "hooked" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "--target"));
    cli_result_free(&result);
}

UTEST(hook_cli, an_unknown_option_is_a_usage_error) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "hooks", "run", "firstboot", "hooked", "--target", "x" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "unknown option or one without a value '--target'"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_engine_refuses_a_hook_that_leaves_the_data_repo) {
    const char *const arguments[] = { "imprint-engine", "hooks", "run", "firstboot", RESOLVED "/escaping-hook.json",
                                      "--data", HOOKS_ROOT "/data" };
    cli_result result = run_engine(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "leaves the data repo"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_engine_needs_the_data_repo) {
    const char *const arguments[] = { "imprint-engine", "hooks", "run", "firstboot", RESOLVED "/hooked.json" };
    cli_result result = run_engine(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(contains(result.error_output, "usage: imprint-engine plan"));
    cli_result_free(&result);
}

#ifndef _WIN32

UTEST(hook_cli, the_manager_runs_a_stage_with_the_environment_set) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "hooks", "run", "session", "environment",
                                      "--personal", HOOKS_ROOT "/personal" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "setup file holds the resolved setup\n"));
    ASSERT_TRUE(contains(result.output, "tskey-fixture-not-a-real-key\n"));
    ASSERT_TRUE(contains(result.output, "/hooks/data\n"));
    ASSERT_STREQ("", result.error_output);
    cli_result_free(&result);
}

UTEST(hook_cli, the_manager_exits_with_the_failed_hooks_code) {
    const char *const arguments[] = { "imprint", "--root", HOOKS_ROOT, "hooks", "run", "firstboot", "failing" };
    cli_result result = run_manager(arguments, COUNT_OF(arguments));
    ASSERT_EQ(7, result.exit_code);
    ASSERT_TRUE(contains(result.output, "failing now\n"));
    ASSERT_FALSE(contains(result.output, "never printed"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_engine_runs_a_stage_from_the_resolved_json) {
    const char *const arguments[] = { "imprint-engine", "hooks", "run", "firstboot", RESOLVED "/hooked.json",
                                      "--data", HOOKS_ROOT "/data" };
    cli_result result = run_engine(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(strstr(result.output, "first ran") < strstr(result.output, "second ran"));
    ASSERT_FALSE(contains(result.output, "build ran"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_engine_exits_with_the_failed_hooks_code) {
    const char *const arguments[] = { "imprint-engine", "hooks", "run", "firstboot", RESOLVED "/failing.json",
                                      "--data", HOOKS_ROOT "/data" };
    cli_result result = run_engine(arguments, COUNT_OF(arguments));
    ASSERT_EQ(7, result.exit_code);
    ASSERT_FALSE(contains(result.output, "never printed"));
    cli_result_free(&result);
}

UTEST(hook_cli, the_engine_installs_firstboot_into_a_root) {
    char *target = path_join(IMPRINT_TEST_WORK, "engine-target");
    error_list errors;
    error_list_init(&errors);
    const char *const remove_arguments[] = { "rm", "-rf", target, NULL };
    const char *const make_arguments[] = { "mkdir", "-p", target, NULL };
    char *ignored = NULL;
    platform_run_capture(remove_arguments, &ignored, &errors);
    free(ignored);
    platform_run_capture(make_arguments, &ignored, &errors);
    free(ignored);
    const char *const arguments[] = { "imprint-engine", "hooks", "install-firstboot", RESOLVED "/hooked.json",
                                      "--data", HOOKS_ROOT "/data", "--target", target };
    cli_result result = run_engine(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(contains(result.output, "the firstboot hooks of setup 'hooked' run on the next boot"));
    char *setup_copy = path_join(target, "var/lib/imprint/firstboot/setup.json");
    char *copied = NULL;
    ASSERT_TRUE(file_read_all(setup_copy, &copied, &errors));
    ASSERT_TRUE(contains(copied, "\"name\": \"hooked\""));
    free(copied);
    free(setup_copy);
    cli_result_free(&result);
    error_list_free(&errors);
    free(target);
}

#endif
