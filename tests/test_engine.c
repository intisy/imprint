#include "engine.h"
#include "exit_codes.h"
#include "platform.h"
#include "text.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

typedef struct engine_result {
    int exit_code;
    char *output;
    char *error_output;
} engine_result;

static engine_result run(const char *const *arguments, size_t count) {
    FILE *output = platform_temporary_file();
    FILE *error_output = platform_temporary_file();
    engine_result result;
    result.exit_code = engine_run((int)count, arguments, output, error_output);
    rewind(output);
    rewind(error_output);
    result.output = platform_read_stream(output);
    result.error_output = platform_read_stream(error_output);
    fclose(output);
    fclose(error_output);
    return result;
}

static void result_free(engine_result *result) {
    free(result->output);
    free(result->error_output);
}

UTEST(engine, plan_prints_every_step) {
    const char *const arguments[] = { "imprint-engine", "plan", IMPRINT_TEST_FIXTURES "/resolved/small.json" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(strstr(result.output, "imprint-engine plan for setup 'example'\n") != NULL);
    ASSERT_TRUE(strstr(result.output, "2. packages (3): git, base-devel, i3-wm\n") != NULL);
    ASSERT_TRUE(strstr(result.output, "3. aur (0)\n") != NULL);
    ASSERT_TRUE(strstr(result.output, "Nothing was installed: this engine only plans.\n") != NULL);
    result_free(&result);
}

UTEST(engine, version) {
    const char *const arguments[] = { "imprint-engine", "--version" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_OK, result.exit_code);
    ASSERT_TRUE(strncmp(result.output, "imprint-engine ", 15) == 0);
    result_free(&result);
}

UTEST(engine, wrong_usage_is_exit_2) {
    const char *const arguments[] = { "imprint-engine", "install" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_USAGE, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "usage: imprint-engine plan") != NULL);
    result_free(&result);
}

UTEST(engine, a_missing_file_is_exit_3) {
    const char *const arguments[] = { "imprint-engine", "plan", IMPRINT_TEST_FIXTURES "/resolved/missing.json" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_ENVIRONMENT, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "cannot read") != NULL);
    result_free(&result);
}

UTEST(engine, another_format_is_exit_1) {
    const char *const arguments[] = { "imprint-engine", "plan", IMPRINT_TEST_FIXTURES "/resolved/old-format.json" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "format 99 is not 2") != NULL);
    result_free(&result);
}

UTEST(engine, a_truncated_file_is_exit_1_and_prints_no_plan) {
    const char *const arguments[] = { "imprint-engine", "plan", IMPRINT_TEST_FIXTURES "/resolved/truncated.json" };
    engine_result result = run(arguments, COUNT_OF(arguments));
    ASSERT_EQ(IMPRINT_EXIT_INVALID, result.exit_code);
    ASSERT_TRUE(strstr(result.error_output, "missing required key 'aur'") != NULL);
    ASSERT_STREQ("", result.output);
    result_free(&result);
}
