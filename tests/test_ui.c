#include "platform.h"
#include "ui.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

typedef struct terminal {
    FILE *input;
    FILE *output;
    ui *interface;
} terminal;

static const char *const ACTIONS[] = { "List setups", "Show a setup", "Validate" };

static terminal open_terminal(const char *typed, bool colors) {
    terminal opened;
    opened.input = platform_temporary_file();
    opened.output = platform_temporary_file();
    fputs(typed, opened.input);
    rewind(opened.input);
    opened.interface = ui_ansi_create(opened.input, opened.output, colors);
    return opened;
}

static char *close_terminal(terminal *opened) {
    ui_destroy(opened->interface);
    rewind(opened->output);
    char *shown = platform_read_stream(opened->output);
    fclose(opened->input);
    fclose(opened->output);
    return shown;
}

static size_t occurrences(const char *text, const char *fragment) {
    size_t count = 0;
    for (const char *found = strstr(text, fragment); found != NULL; found = strstr(found + 1, fragment)) {
        count++;
    }
    return count;
}

UTEST(ui, menu_returns_the_chosen_item) {
    terminal opened = open_terminal("2\n", false);
    size_t chosen = 0;
    ASSERT_TRUE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)1, chosen);
    ASSERT_TRUE(strstr(shown, " 2) Show a setup") != NULL);
    ASSERT_TRUE(strstr(shown, "\x1b[") == NULL);
    free(shown);
}

UTEST(ui, menu_accepts_windows_line_endings) {
    terminal opened = open_terminal("3\r\n", false);
    size_t chosen = 0;
    ASSERT_TRUE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    free(close_terminal(&opened));
    ASSERT_EQ((size_t)2, chosen);
}

UTEST(ui, menu_asks_again_after_a_letter_or_a_number_out_of_range) {
    terminal opened = open_terminal("x\n9\n0\n-1\n1\n", false);
    size_t chosen = 5;
    ASSERT_TRUE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)0, chosen);
    ASSERT_EQ((size_t)4, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, the_end_of_input_cancels_instead_of_looping) {
    terminal opened = open_terminal("abc", false);
    size_t chosen = 0;
    ASSERT_FALSE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, the_end_of_input_with_nothing_typed_cancels_without_a_hint) {
    terminal opened = open_terminal("", false);
    size_t chosen = 7;
    ASSERT_FALSE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)7, chosen);
    ASSERT_EQ((size_t)0, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, the_end_of_input_after_a_bad_answer_cancels_a_menu) {
    terminal opened = open_terminal("x\n9\n", false);
    size_t chosen = 7;
    ASSERT_FALSE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)7, chosen);
    ASSERT_EQ((size_t)2, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, the_end_of_input_after_a_bad_answer_cancels_choose_one) {
    terminal opened = open_terminal("9\n", false);
    size_t chosen = 7;
    ASSERT_FALSE(ui_choose_one(opened.interface, "Which?", ACTIONS, 3, 1, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)7, chosen);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, q_goes_back) {
    terminal opened = open_terminal("q\n", false);
    size_t chosen = 0;
    ASSERT_FALSE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    free(close_terminal(&opened));
}

UTEST(ui, choose_one_takes_the_preselected_item_on_enter) {
    terminal opened = open_terminal("\n", false);
    size_t chosen = 0;
    ASSERT_TRUE(ui_choose_one(opened.interface, "Which?", ACTIONS, 3, 2, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)2, chosen);
    ASSERT_TRUE(strstr(shown, "Enter for 3") != NULL);
    free(shown);
}

UTEST(ui, an_over_long_line_is_rejected_whole) {
    char typed[400];
    memset(typed, '1', 300);
    strcpy(typed + 300, "\n2\n");
    terminal opened = open_terminal(typed, false);
    size_t chosen = 0;
    ASSERT_TRUE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)1, chosen);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, an_over_long_line_at_the_end_of_input_cancels) {
    char typed[400];
    memset(typed, '1', 300);
    typed[300] = '\0';
    terminal opened = open_terminal(typed, false);
    size_t chosen = 7;
    ASSERT_FALSE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    char *shown = close_terminal(&opened);
    ASSERT_EQ((size_t)7, chosen);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type one of the numbers shown."));
    free(shown);
}

UTEST(ui, choose_many_switches_and_accepts) {
    terminal opened = open_terminal("1,3\n1,9\n\n", false);
    bool selected[3] = { false, false, false };
    ASSERT_TRUE(ui_choose_many(opened.interface, "Which apps?", ACTIONS, 3, selected));
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(selected[0]);
    ASSERT_FALSE(selected[1]);
    ASSERT_TRUE(selected[2]);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type numbers from the list"));
    free(shown);
}

UTEST(ui, choose_many_cancel_keeps_the_selection) {
    terminal opened = open_terminal("1\nq\n", false);
    bool selected[3] = { false, true, false };
    ASSERT_FALSE(ui_choose_many(opened.interface, "Which apps?", ACTIONS, 3, selected));
    free(close_terminal(&opened));
    ASSERT_FALSE(selected[0]);
    ASSERT_TRUE(selected[1]);
}

UTEST(ui, choose_many_asks_again_after_a_letter_and_cancels_at_the_end_of_input) {
    terminal opened = open_terminal("x\n2\nabc", false);
    bool selected[3] = { true, false, false };
    ASSERT_FALSE(ui_choose_many(opened.interface, "Which apps?", ACTIONS, 3, selected));
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(selected[0]);
    ASSERT_FALSE(selected[1]);
    ASSERT_EQ((size_t)2, occurrences(shown, "Type numbers from the list"));
    free(shown);
}

UTEST(ui, choose_many_rejects_an_over_long_line_and_cancels_at_the_end_of_input) {
    char typed[400];
    memset(typed, '1', 300);
    strcpy(typed + 300, "\n");
    terminal opened = open_terminal(typed, false);
    bool selected[3] = { false, false, false };
    ASSERT_FALSE(ui_choose_many(opened.interface, "Which apps?", ACTIONS, 3, selected));
    char *shown = close_terminal(&opened);
    ASSERT_FALSE(selected[0]);
    ASSERT_EQ((size_t)1, occurrences(shown, "Type numbers from the list"));
    free(shown);
}

UTEST(ui, text_takes_the_default_on_enter) {
    terminal opened = open_terminal("\nfinn\n", false);
    char *first = NULL;
    char *second = NULL;
    ASSERT_TRUE(ui_text(opened.interface, "User name", "user", &first));
    ASSERT_TRUE(ui_text(opened.interface, "User name", "user", &second));
    ASSERT_FALSE(ui_text(opened.interface, "User name", "user", &second));
    free(close_terminal(&opened));
    ASSERT_STREQ("user", first);
    ASSERT_STREQ("finn", second);
    free(first);
    free(second);
}

UTEST(ui, text_rejects_an_over_long_line_and_cancels_at_the_end_of_input) {
    char typed[400];
    memset(typed, 'a', 300);
    strcpy(typed + 300, "\nfinn\n");
    terminal opened = open_terminal(typed, false);
    char *first = NULL;
    char *second = NULL;
    ASSERT_TRUE(ui_text(opened.interface, "User name", "user", &first));
    ASSERT_FALSE(ui_text(opened.interface, "User name", "user", &second));
    char *shown = close_terminal(&opened);
    ASSERT_STREQ("finn", first);
    ASSERT_EQ((size_t)1, occurrences(shown, "That is longer than 255 characters."));
    free(shown);
    free(first);
}

UTEST(ui, text_cancels_at_the_end_of_input_after_an_over_long_line) {
    char typed[400];
    memset(typed, 'a', 300);
    strcpy(typed + 300, "\n");
    terminal opened = open_terminal(typed, false);
    char *answer = NULL;
    ASSERT_FALSE(ui_text(opened.interface, "User name", "user", &answer));
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(answer == NULL);
    ASSERT_EQ((size_t)1, occurrences(shown, "That is longer than 255 characters."));
    free(shown);
}

UTEST(ui, confirm_reads_yes_no_and_the_default) {
    terminal opened = open_terminal("\nn\nmaybe\nYES\n", false);
    bool answers[3] = { false, true, false };
    ASSERT_TRUE(ui_confirm(opened.interface, "Erase it?", true, &answers[0]));
    ASSERT_TRUE(ui_confirm(opened.interface, "Erase it?", true, &answers[1]));
    ASSERT_TRUE(ui_confirm(opened.interface, "Erase it?", false, &answers[2]));
    bool ended = true;
    ASSERT_FALSE(ui_confirm(opened.interface, "Erase it?", true, &ended));
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(answers[0]);
    ASSERT_FALSE(answers[1]);
    ASSERT_TRUE(answers[2]);
    ASSERT_EQ((size_t)1, occurrences(shown, "Answer y or n."));
    free(shown);
}

UTEST(ui, confirm_cancels_at_the_end_of_input_after_a_bad_answer) {
    terminal opened = open_terminal("maybe\n", false);
    bool answer = true;
    ASSERT_FALSE(ui_confirm(opened.interface, "Erase it?", false, &answer));
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(answer);
    ASSERT_EQ((size_t)1, occurrences(shown, "Answer y or n."));
    free(shown);
}

UTEST(ui, confirm_rejects_an_over_long_line_and_cancels_at_the_end_of_input) {
    char typed[400];
    memset(typed, 'y', 300);
    strcpy(typed + 300, "\n");
    terminal opened = open_terminal(typed, false);
    bool answer = false;
    ASSERT_FALSE(ui_confirm(opened.interface, "Erase it?", false, &answer));
    char *shown = close_terminal(&opened);
    ASSERT_FALSE(answer);
    ASSERT_EQ((size_t)1, occurrences(shown, "Answer y or n."));
    free(shown);
}

UTEST(ui, colors_paint_the_title_and_errors) {
    terminal opened = open_terminal("1\n", true);
    size_t chosen = 0;
    ASSERT_TRUE(ui_menu(opened.interface, "imprint", ACTIONS, 3, &chosen));
    ui_log(opened.interface, UI_ERROR, "it broke");
    char *shown = close_terminal(&opened);
    ASSERT_TRUE(strstr(shown, "\x1b[1mimprint") != NULL);
    ASSERT_TRUE(strstr(shown, "\x1b[31merror: ") != NULL);
    ASSERT_TRUE(strstr(shown, "it broke\n") != NULL);
    free(shown);
}

UTEST(ui, without_colors_progress_prints_once_when_done) {
    terminal opened = open_terminal("", false);
    ui_progress(opened.interface, "Copying", 1, 3);
    ui_progress(opened.interface, "Copying", 2, 3);
    ui_progress(opened.interface, "Copying", 3, 3);
    char *shown = close_terminal(&opened);
    ASSERT_STREQ("Copying 3/3\n", shown);
    free(shown);
}

UTEST(ui_scripted, answers_are_used_in_order_and_recorded) {
    const char *const answers[] = { "2", "yes", "finn", "1,3" };
    ui *scripted = ui_scripted_create(answers, 4);
    size_t chosen = 0;
    bool confirmed = false;
    char *name = NULL;
    bool selected[3] = { false, true, false };
    ASSERT_TRUE(ui_menu(scripted, "imprint", ACTIONS, 3, &chosen));
    ASSERT_TRUE(ui_confirm(scripted, "Erase it?", false, &confirmed));
    ASSERT_TRUE(ui_text(scripted, "User name", "user", &name));
    ASSERT_TRUE(ui_choose_many(scripted, "Which apps?", ACTIONS, 3, selected));
    ui_log(scripted, UI_WARNING, "careful");
    ASSERT_EQ((size_t)1, chosen);
    ASSERT_TRUE(confirmed);
    ASSERT_STREQ("finn", name);
    ASSERT_TRUE(selected[0]);
    ASSERT_FALSE(selected[1]);
    ASSERT_TRUE(selected[2]);
    ASSERT_TRUE(strstr(ui_scripted_transcript(scripted), "menu: imprint\n") != NULL);
    ASSERT_TRUE(strstr(ui_scripted_transcript(scripted), "log: warning: careful\n") != NULL);
    ASSERT_FALSE(ui_scripted_exhausted(scripted));
    free(name);
    ui_destroy(scripted);
}

UTEST(ui_scripted, running_out_of_answers_cancels_and_is_noticed) {
    ui *scripted = ui_scripted_create(NULL, 0);
    size_t chosen = 0;
    ASSERT_FALSE(ui_menu(scripted, "imprint", ACTIONS, 3, &chosen));
    ASSERT_TRUE(ui_scripted_exhausted(scripted));
    ui_destroy(scripted);
}

UTEST(ui_scripted, a_cancel_answer_does_not_clear_exhausted) {
    const char *const answers[] = { "9", "cancel" };
    ui *scripted = ui_scripted_create(answers, 2);
    size_t chosen = 0;
    ASSERT_FALSE(ui_menu(scripted, "imprint", ACTIONS, 3, &chosen));
    ASSERT_TRUE(ui_scripted_exhausted(scripted));
    ASSERT_FALSE(ui_menu(scripted, "imprint", ACTIONS, 3, &chosen));
    ASSERT_TRUE(ui_scripted_exhausted(scripted));
    ui_destroy(scripted);
}

UTEST(ui_scripted, a_cancel_answer_alone_is_not_exhaustion) {
    const char *const answers[] = { "cancel" };
    ui *scripted = ui_scripted_create(answers, 1);
    size_t chosen = 0;
    ASSERT_FALSE(ui_menu(scripted, "imprint", ACTIONS, 3, &chosen));
    ASSERT_FALSE(ui_scripted_exhausted(scripted));
    ui_destroy(scripted);
}
