#include "platform.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

UTEST(platform, list_files_returns_sorted_regular_files_without_hidden_ones) {
    string_list names;
    string_list_init(&names);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(platform_list_files(IMPRINT_TEST_FIXTURES "/listing", &names, &errors));
    char *joined = string_list_join(&names, ",");
    ASSERT_STREQ("a.toml,b.toml", joined);
    free(joined);
    string_list_free(&names);
}

UTEST(platform, list_files_of_a_missing_directory_is_an_error) {
    string_list names;
    string_list_init(&names);
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(platform_list_files(IMPRINT_TEST_FIXTURES "/listing/missing", &names, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "cannot open the directory"));
    error_list_free(&errors);
    string_list_free(&names);
}

UTEST(platform, is_directory_and_is_file) {
    ASSERT_TRUE(platform_is_directory(IMPRINT_TEST_FIXTURES "/listing/nested"));
    ASSERT_FALSE(platform_is_directory(IMPRINT_TEST_FIXTURES "/listing/a.toml"));
    ASSERT_TRUE(platform_is_file(IMPRINT_TEST_FIXTURES "/listing/a.toml"));
    ASSERT_FALSE(platform_is_file(IMPRINT_TEST_FIXTURES "/listing/nested"));
    ASSERT_FALSE(platform_is_file(IMPRINT_TEST_FIXTURES "/listing/missing.toml"));
}

UTEST(platform, temporary_file_reads_back_what_was_written) {
    FILE *stream = platform_temporary_file();
    ASSERT_TRUE(stream != NULL);
    fputs("written\n", stream);
    rewind(stream);
    char *text = platform_read_stream(stream);
    ASSERT_STREQ("written\n", text);
    free(text);
    fclose(stream);
}

#ifdef _WIN32

#define ROOT_WITH_SPACES_AND_TRAILING_BACKSLASH "C:\\Users\\Some One\\imprint\\"

UTEST(platform, quote_keeps_spaces_and_doubles_a_trailing_backslash) {
    char *quoted = platform_quote_argument(ROOT_WITH_SPACES_AND_TRAILING_BACKSLASH);
    ASSERT_STREQ("\"C:\\Users\\Some One\\imprint\\\\\"", quoted);
    free(quoted);
}

UTEST(platform, quote_refuses_what_cmd_would_reinterpret) {
    ASSERT_TRUE(platform_quote_argument("a\"b") == NULL);
    ASSERT_TRUE(platform_quote_argument("%PATH%") == NULL);
}

UTEST(platform, run_capture_returns_the_output) {
    const char *const arguments[] = { "cmd", "/c", "echo hello", NULL };
    error_list errors;
    error_list_init(&errors);
    char *output = NULL;
    ASSERT_EQ(0, platform_run_capture(arguments, &output, &errors));
    ASSERT_TRUE(strstr(output, "hello") != NULL);
    free(output);
}

UTEST(platform, run_capture_refuses_an_argument_cmd_would_reinterpret_and_says_why) {
    const char *const arguments[] = { "cmd", "/c", "echo %PATH%", NULL };
    error_list errors;
    error_list_init(&errors);
    char *output = NULL;
    ASSERT_EQ(-1, platform_run_capture(arguments, &output, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "percent sign"));
    error_list_free(&errors);
}

#else

#define ROOT_WITH_SPACES_AND_TRAILING_BACKSLASH "/home/Some One/imprint\\"

UTEST(platform, quote_keeps_spaces_and_escapes_single_quotes) {
    char *quoted = platform_quote_argument("/home/Some One/it's");
    ASSERT_STREQ("'/home/Some One/it'\\''s'", quoted);
    free(quoted);
}

UTEST(platform, run_capture_returns_the_output_and_the_exit_status) {
    const char *const arguments[] = { "sh", "-c", "echo hello; echo problem >&2; exit 3", NULL };
    error_list errors;
    error_list_init(&errors);
    char *output = NULL;
    ASSERT_EQ(3, platform_run_capture(arguments, &output, &errors));
    ASSERT_STREQ("hello\nproblem\n", output);
    free(output);
}

UTEST(platform, run_capture_passes_an_argument_with_spaces_intact) {
    const char *const arguments[] = { "printf", "%s|", "/home/Some One/imprint", NULL };
    error_list errors;
    error_list_init(&errors);
    char *output = NULL;
    ASSERT_EQ(0, platform_run_capture(arguments, &output, &errors));
    ASSERT_STREQ("/home/Some One/imprint|", output);
    free(output);
}

#endif

UTEST(platform, run_capture_delivers_a_root_with_spaces_and_a_trailing_backslash_intact) {
    const char *const arguments[] = { IMPRINT_TEST_ARGUMENT_ECHO, ROOT_WITH_SPACES_AND_TRAILING_BACKSLASH, "plain", NULL };
    error_list errors;
    error_list_init(&errors);
    char *output = NULL;
    ASSERT_EQ(0, platform_run_capture(arguments, &output, &errors));
    char *expected = text_format("[%s]\n[plain]\n", ROOT_WITH_SPACES_AND_TRAILING_BACKSLASH);
    ASSERT_STREQ(expected, output);
    free(expected);
    free(output);
}
