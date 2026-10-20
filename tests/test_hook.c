#include "hook.h"
#include "platform.h"
#include "setup.h"
#include "text.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

#define HOOKS_DATA IMPRINT_TEST_FIXTURES "/hooks/data"
#define BROKEN IMPRINT_TEST_FIXTURES "/broken/data"

static bool load_broken(const char *name, error_list *errors) {
    setup loaded;
    bool valid = setup_load(BROKEN, name, &loaded, errors);
    setup_free(&loaded);
    return valid;
}

static bool broken_reports(const char *name, const char *fragment) {
    error_list errors;
    error_list_init(&errors);
    bool reported = !load_broken(name, &errors) && error_list_mentions(&errors, fragment);
    if (!reported) {
        error_list_print(&errors, stderr);
    }
    error_list_free(&errors);
    return reported;
}

static bool read_hooks_of(const char *name, hook_list *hooks) {
    setup loaded;
    error_list errors;
    error_list_init(&errors);
    hook_list_init(hooks);
    bool valid = setup_load(HOOKS_DATA, name, &loaded, &errors) && hooks_read(loaded.merged, hooks, &errors);
    error_list_print(&errors, stderr);
    error_list_free(&errors);
    setup_free(&loaded);
    return valid;
}

static char *stage_scripts(const hook_list *hooks, hook_stage stage) {
    string_list scripts;
    string_list_init(&scripts);
    hook_list_scripts(hooks, stage, &scripts);
    char *joined = string_list_join(&scripts, ",");
    string_list_free(&scripts);
    return joined;
}

UTEST(hook, every_stage_name_reads_back) {
    const char *const names[] = { "build", "install", "firstboot", "session" };
    for (size_t index = 0; index < COUNT_OF(names); index++) {
        hook_stage stage = HOOK_STAGE_BUILD;
        ASSERT_TRUE(hook_stage_parse(names[index], &stage));
        ASSERT_STREQ(names[index], hook_stage_name(stage));
    }
    hook_stage ignored = HOOK_STAGE_BUILD;
    ASSERT_FALSE(hook_stage_parse("boot", &ignored));
    ASSERT_FALSE(hook_stage_parse("Firstboot", &ignored));
}

UTEST(hook, an_unknown_stage_names_the_stages) {
    ASSERT_TRUE(broken_reports("hook-stage", "hook-stage.toml:4: hooks[0].stage: unknown hook stage 'boot': one of build, install, "
                                             "firstboot, session"));
}

UTEST(hook, a_missing_script_is_named_with_its_path) {
    ASSERT_TRUE(broken_reports("hook-missing", "hook script 'hooks/absent.sh' is not in the data repo"));
}

UTEST(hook, a_script_that_climbs_out_of_the_repo_is_refused) {
    ASSERT_TRUE(broken_reports("hook-escape", "hook script 'hooks/../../outside.sh' leaves the data repo"));
}

UTEST(hook, an_absolute_script_is_refused) {
    ASSERT_TRUE(broken_reports("hook-absolute", "hook script '/etc/passwd' leaves the data repo"));
}

UTEST(hook, a_script_with_a_space_is_refused) {
    ASSERT_TRUE(broken_reports("hook-characters", "hook script 'hooks/my hook.sh' may hold only letters"));
}

UTEST(hook, a_hook_without_a_stage_is_refused) {
    ASSERT_TRUE(broken_reports("hook-stageless", "missing required key 'stage'"));
}

UTEST(hook, hooks_must_be_an_array_of_tables) {
    ASSERT_TRUE(broken_reports("hook-shape", "must be an array of [[hooks]] tables, found string"));
}

UTEST(hook, an_unknown_key_in_a_hook_is_refused) {
    ASSERT_TRUE(broken_reports("hook-keys", "hooks[0].when: unknown key"));
}

UTEST(hook, a_base_runs_its_hooks_before_its_child_and_a_repeated_hook_runs_once) {
    hook_list hooks;
    ASSERT_TRUE(read_hooks_of("hooked", &hooks));
    ASSERT_EQ((size_t)3, hooks.count);
    char *firstboot = stage_scripts(&hooks, HOOK_STAGE_FIRSTBOOT);
    char *build = stage_scripts(&hooks, HOOK_STAGE_BUILD);
    ASSERT_STREQ("hooks/first.sh,hooks/second.sh", firstboot);
    ASSERT_STREQ("hooks/build.sh", build);
    free(firstboot);
    free(build);
    hook_list_free(&hooks);
}

UTEST(hook, a_setup_without_hooks_reads_as_empty) {
    hook_list hooks;
    ASSERT_TRUE(read_hooks_of("plain", &hooks));
    ASSERT_EQ((size_t)0, hooks.count);
    hook_list_free(&hooks);
}

UTEST(hook, a_merged_setup_from_elsewhere_is_checked_again) {
    error_list errors;
    error_list_init(&errors);
    document_node *handed_over = document_parse_json("{\"hooks\": [{\"stage\": \"firstboot\", \"script\": \"../x.sh\"}]}",
                                               "handed-over.json", &errors);
    ASSERT_TRUE(handed_over != NULL);
    hook_list hooks;
    ASSERT_FALSE(hooks_read(handed_over, &hooks, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "leaves the data repo"));
    hook_list_free(&hooks);
    document_free(handed_over);
    error_list_free(&errors);
}

#ifndef _WIN32

typedef struct stage_run {
    int status;
    char *log;
    error_list errors;
} stage_run;

static stage_run run_stage_of(const char *name, hook_stage stage, const char *data_directory) {
    stage_run run;
    error_list_init(&run.errors);
    hook_list hooks;
    read_hooks_of(name, &hooks);
    FILE *log = platform_temporary_file();
    hook_environment environment = { .setup_name = name, .setup_file = "/nonexistent/setup.json",
                                     .data_directory = data_directory, .personal_directory = NULL, .log = log };
    run.status = hooks_run_stage(&hooks, stage, &environment, &run.errors);
    rewind(log);
    run.log = platform_read_stream(log);
    fclose(log);
    hook_list_free(&hooks);
    return run;
}

static void stage_run_free(stage_run *run) {
    free(run->log);
    error_list_free(&run->errors);
}

static bool appears_before(const char *text, const char *first, const char *second) {
    const char *first_at = strstr(text, first);
    const char *second_at = strstr(text, second);
    return first_at != NULL && second_at != NULL && first_at < second_at;
}

UTEST(hook_runner, runs_the_stage_in_order_and_nothing_of_another_stage) {
    stage_run run = run_stage_of("hooked", HOOK_STAGE_FIRSTBOOT, HOOKS_DATA);
    ASSERT_EQ(0, run.status);
    ASSERT_TRUE(appears_before(run.log, "imprint: firstboot hook 1 of 2: hooks/first.sh\n", "first ran in stage firstboot\n"));
    ASSERT_TRUE(appears_before(run.log, "first ran in stage firstboot\n", "imprint: firstboot hook 2 of 2: hooks/second.sh\n"));
    ASSERT_TRUE(appears_before(run.log, "second ran\n", "every firstboot hook of setup 'hooked' succeeded"));
    ASSERT_TRUE(strstr(run.log, "build ran") == NULL);
    stage_run_free(&run);
}

UTEST(hook_runner, stops_at_the_first_failure_with_its_exit_code) {
    stage_run run = run_stage_of("failing", HOOK_STAGE_FIRSTBOOT, HOOKS_DATA);
    ASSERT_EQ(7, run.status);
    ASSERT_TRUE(appears_before(run.log, "first ran", "failing now\n"));
    ASSERT_TRUE(strstr(run.log, "imprint: hooks/fails.sh failed with exit code 7; the firstboot hooks after it did not run\n") != NULL);
    ASSERT_TRUE(strstr(run.log, "never printed") == NULL);
    ASSERT_TRUE(strstr(run.log, "succeeded") == NULL);
    stage_run_free(&run);
}

UTEST(hook_runner, a_stage_without_hooks_runs_nothing_and_succeeds) {
    stage_run run = run_stage_of("hooked", HOOK_STAGE_SESSION, HOOKS_DATA);
    ASSERT_EQ(0, run.status);
    ASSERT_STREQ("imprint: setup 'hooked' has no session hooks\n", run.log);
    stage_run_free(&run);
}

UTEST(hook_runner, a_script_missing_at_run_time_stops_before_any_hook_runs) {
    stage_run run = run_stage_of("hooked", HOOK_STAGE_FIRSTBOOT, IMPRINT_TEST_FIXTURES "/empty-root/data");
    ASSERT_EQ(-1, run.status);
    ASSERT_TRUE(error_list_mentions(&run.errors, "hook script 'hooks/second.sh' is not in the data repo"));
    ASSERT_TRUE(strstr(run.log, "first ran") == NULL);
    stage_run_free(&run);
}

#endif
