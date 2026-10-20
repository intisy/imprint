#include "ui.h"

#include "platform.h"
#include "text.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

enum { ANSWER_LENGTH = 256 };

typedef enum answer_status { ANSWER_READ, ANSWER_TOO_LONG, ANSWER_INPUT_ENDED } answer_status;

typedef struct ansi_state {
    FILE *input;
    FILE *output;
    bool colors;
} ansi_state;

static ansi_state *state_of(ui *self) {
    return self->state;
}

static void paint(ansi_state *state, const char *code, const char *text) {
    if (state->colors) {
        fprintf(state->output, "\x1b[%sm%s\x1b[0m", code, text);
    } else {
        fputs(text, state->output);
    }
}

static void line_break(ansi_state *state) {
    fputc('\n', state->output);
}

static void hint(ansi_state *state, const char *text) {
    paint(state, "33", text);
    line_break(state);
}

static answer_status read_answer(ansi_state *state, char *answer) {
    fflush(state->output);
    if (fgets(answer, ANSWER_LENGTH, state->input) == NULL) {
        return ANSWER_INPUT_ENDED;
    }
    size_t length = strcspn(answer, "\r\n");
    bool whole_line = answer[length] != '\0' || feof(state->input);
    answer[length] = '\0';
    if (whole_line) {
        return ANSWER_READ;
    }
    int character = 0;
    while ((character = fgetc(state->input)) != EOF && character != '\n') {
    }
    return ANSWER_TOO_LONG;
}

static bool parse_number(const char *text, size_t count, size_t *index) {
    if (!isdigit((unsigned char)text[0])) {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (*end != '\0' || value < 1 || value > count) {
        return false;
    }
    *index = (size_t)value - 1;
    return true;
}

static bool is_cancel(const char *answer) {
    return strcmp(answer, "q") == 0;
}

static void print_items(ansi_state *state, const char *const *items, size_t count, const bool *selected) {
    for (size_t index = 0; index < count; index++) {
        if (selected != NULL) {
            fprintf(state->output, " %2zu) [%c] %s\n", index + 1, selected[index] ? 'x' : ' ', items[index]);
        } else {
            fprintf(state->output, " %2zu) %s\n", index + 1, items[index]);
        }
    }
}

static void title(ansi_state *state, const char *text) {
    paint(state, "1", text);
    line_break(state);
}

static bool choose(ui *self, const char *heading, const char *const *items, size_t count, const size_t *preselected,
                   size_t *chosen) {
    ansi_state *state = state_of(self);
    title(state, heading);
    if (count == 0) {
        hint(state, "There is nothing to choose from.");
        return false;
    }
    print_items(state, items, count, NULL);
    char answer[ANSWER_LENGTH];
    for (;;) {
        if (preselected != NULL) {
            fprintf(state->output, "Number, Enter for %zu, q to go back: ", *preselected + 1);
        } else {
            fputs("Number, q to go back: ", state->output);
        }
        answer_status status = read_answer(state, answer);
        if (status == ANSWER_INPUT_ENDED) {
            line_break(state);
            return false;
        }
        if (status == ANSWER_READ && is_cancel(answer)) {
            return false;
        }
        if (status == ANSWER_READ && preselected != NULL && answer[0] == '\0') {
            *chosen = *preselected;
            return true;
        }
        if (status == ANSWER_READ && parse_number(answer, count, chosen)) {
            return true;
        }
        hint(state, "Type one of the numbers shown.");
    }
}

static bool ansi_menu(ui *self, const char *heading, const char *const *items, size_t count, size_t *chosen) {
    return choose(self, heading, items, count, NULL, chosen);
}

static bool ansi_choose_one(ui *self, const char *prompt, const char *const *items, size_t count, size_t preselected,
                            size_t *chosen) {
    return choose(self, prompt, items, count, preselected < count ? &preselected : NULL, chosen);
}

static bool toggle_numbers(char *answer, size_t count, bool *selection) {
    for (char *token = strtok(answer, ", "); token != NULL; token = strtok(NULL, ", ")) {
        size_t index = 0;
        if (!parse_number(token, count, &index)) {
            return false;
        }
        selection[index] = !selection[index];
    }
    return true;
}

static bool ansi_choose_many(ui *self, const char *prompt, const char *const *items, size_t count, bool *selected) {
    ansi_state *state = state_of(self);
    bool *draft = memory_allocate(count * sizeof *draft);
    bool *attempt = memory_allocate(count * sizeof *attempt);
    memcpy(draft, selected, count * sizeof *draft);
    char answer[ANSWER_LENGTH];
    bool accepted = false;
    for (;;) {
        title(state, prompt);
        print_items(state, items, count, draft);
        fputs("Numbers to switch, Enter to accept, q to go back: ", state->output);
        answer_status status = read_answer(state, answer);
        if (status == ANSWER_INPUT_ENDED || (status == ANSWER_READ && is_cancel(answer))) {
            break;
        }
        if (status == ANSWER_READ && answer[0] == '\0') {
            accepted = true;
            break;
        }
        memcpy(attempt, draft, count * sizeof *attempt);
        if (status == ANSWER_READ && toggle_numbers(answer, count, attempt)) {
            memcpy(draft, attempt, count * sizeof *draft);
        } else {
            hint(state, "Type numbers from the list, separated by commas.");
        }
    }
    if (accepted) {
        memcpy(selected, draft, count * sizeof *selected);
    }
    free(draft);
    free(attempt);
    return accepted;
}

static bool ansi_text(ui *self, const char *prompt, const char *default_value, char **answer) {
    ansi_state *state = state_of(self);
    char line[ANSWER_LENGTH];
    for (;;) {
        if (default_value != NULL && default_value[0] != '\0') {
            fprintf(state->output, "%s [%s]: ", prompt, default_value);
        } else {
            fprintf(state->output, "%s: ", prompt);
        }
        answer_status status = read_answer(state, line);
        if (status == ANSWER_INPUT_ENDED) {
            line_break(state);
            return false;
        }
        if (status == ANSWER_READ) {
            *answer = text_duplicate(line[0] == '\0' && default_value != NULL ? default_value : line);
            return true;
        }
        hint(state, "That is longer than 255 characters.");
    }
}

static bool ansi_secret(ui *self, const char *prompt, char **answer) {
    ansi_state *state = state_of(self);
    char line[ANSWER_LENGTH];
    for (;;) {
        fprintf(state->output, "%s (not shown): ", prompt);
        bool hidden = platform_set_echo(state->input, false);
        answer_status status = read_answer(state, line);
        if (hidden) {
            platform_set_echo(state->input, true);
            line_break(state);
        }
        if (status == ANSWER_INPUT_ENDED) {
            if (!hidden) {
                line_break(state);
            }
            return false;
        }
        if (status == ANSWER_READ) {
            *answer = text_duplicate(line);
            memset(line, 0, sizeof line);
            return true;
        }
        hint(state, "That is longer than 255 characters.");
    }
}

static void lowercase(char *text) {
    for (; *text != '\0'; text++) {
        *text = (char)tolower((unsigned char)*text);
    }
}

static bool ansi_confirm(ui *self, const char *question, bool default_answer, bool *answer) {
    ansi_state *state = state_of(self);
    char line[ANSWER_LENGTH];
    for (;;) {
        fprintf(state->output, "%s [%s]: ", question, default_answer ? "Y/n" : "y/N");
        answer_status status = read_answer(state, line);
        if (status == ANSWER_INPUT_ENDED) {
            line_break(state);
            return false;
        }
        lowercase(line);
        if (status == ANSWER_READ && line[0] == '\0') {
            *answer = default_answer;
            return true;
        }
        if (status == ANSWER_READ && (strcmp(line, "y") == 0 || strcmp(line, "yes") == 0)) {
            *answer = true;
            return true;
        }
        if (status == ANSWER_READ && (strcmp(line, "n") == 0 || strcmp(line, "no") == 0)) {
            *answer = false;
            return true;
        }
        hint(state, "Answer y or n.");
    }
}

static void ansi_progress(ui *self, const char *label, size_t done, size_t total) {
    ansi_state *state = state_of(self);
    if (state->colors) {
        fprintf(state->output, "\r\x1b[2K%s %zu/%zu", label, done, total);
        if (done >= total) {
            line_break(state);
        }
    } else if (done >= total) {
        fprintf(state->output, "%s %zu/%zu\n", label, done, total);
    }
    fflush(state->output);
}

static void ansi_log(ui *self, ui_level level, const char *message) {
    ansi_state *state = state_of(self);
    if (level == UI_ERROR) {
        paint(state, "31", "error: ");
    } else if (level == UI_WARNING) {
        paint(state, "33", "warning: ");
    }
    fputs(message, state->output);
    line_break(state);
}

static void ansi_destroy(ui *self) {
    free(self->state);
    free(self);
}

static const ui_operations ANSI_OPERATIONS = {
    .menu = ansi_menu,
    .choose_one = ansi_choose_one,
    .choose_many = ansi_choose_many,
    .text = ansi_text,
    .secret = ansi_secret,
    .confirm = ansi_confirm,
    .progress = ansi_progress,
    .log = ansi_log,
    .destroy = ansi_destroy,
};

ui *ui_ansi_create(FILE *input, FILE *output, bool colors) {
    ansi_state *state = memory_allocate(sizeof *state);
    state->input = input;
    state->output = output;
    state->colors = colors;
    ui *created = memory_allocate(sizeof *created);
    created->operations = &ANSI_OPERATIONS;
    created->state = state;
    return created;
}

#ifdef _WIN32
/** @implNote Windows 10 and later draw ANSI escape codes only after the console opts in to VT mode. */
static bool enable_virtual_terminal(FILE *output) {
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(output));
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return false;
    }
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#else
static bool enable_virtual_terminal(FILE *output) {
    (void)output;
    return true;
}
#endif

bool ui_ansi_colors_available(FILE *output) {
    return getenv("NO_COLOR") == NULL && platform_terminal_is_interactive(output) && enable_virtual_terminal(output);
}
