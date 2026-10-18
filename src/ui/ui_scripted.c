#include "ui.h"

#include "string_list.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

typedef struct scripted_state {
    string_list answers;
    size_t next;
    char *transcript;
    bool exhausted;
} scripted_state;

static scripted_state *state_of(const ui *self) {
    return self->state;
}

static void record(scripted_state *state, const char *kind, const char *text) {
    char *longer = text_format("%s%s: %s\n", state->transcript, kind, text);
    free(state->transcript);
    state->transcript = longer;
}

static const char *next_answer(scripted_state *state) {
    if (state->next >= state->answers.count) {
        state->exhausted = true;
        return NULL;
    }
    const char *answer = state->answers.items[state->next++];
    return strcmp(answer, "cancel") == 0 ? NULL : answer;
}

static bool unusable(scripted_state *state, const char *answer) {
    record(state, "unusable answer", answer);
    state->exhausted = true;
    return false;
}

static bool to_index(scripted_state *state, const char *answer, size_t count, size_t *index) {
    char *end = NULL;
    unsigned long value = strtoul(answer, &end, 10);
    if (end == answer || *end != '\0' || value < 1 || value > count) {
        return unusable(state, answer);
    }
    *index = (size_t)value - 1;
    return true;
}

static bool scripted_pick(ui *self, const char *kind, const char *heading, size_t count, size_t *chosen) {
    scripted_state *state = state_of(self);
    record(state, kind, heading);
    const char *answer = next_answer(state);
    return answer != NULL && to_index(state, answer, count, chosen);
}

static bool scripted_menu(ui *self, const char *heading, const char *const *items, size_t count, size_t *chosen) {
    (void)items;
    return scripted_pick(self, "menu", heading, count, chosen);
}

static bool scripted_choose_one(ui *self, const char *prompt, const char *const *items, size_t count,
                                size_t preselected, size_t *chosen) {
    (void)items;
    (void)preselected;
    return scripted_pick(self, "choose one", prompt, count, chosen);
}

static bool scripted_choose_many(ui *self, const char *prompt, const char *const *items, size_t count, bool *selected) {
    (void)items;
    scripted_state *state = state_of(self);
    record(state, "choose many", prompt);
    const char *answer = next_answer(state);
    if (answer == NULL) {
        return false;
    }
    bool *chosen = memory_allocate(count * sizeof *chosen);
    memset(chosen, 0, count * sizeof *chosen);
    char *numbers = text_duplicate(answer);
    bool usable = true;
    for (char *token = strtok(numbers, ","); usable && token != NULL; token = strtok(NULL, ",")) {
        size_t index = 0;
        usable = to_index(state, token, count, &index);
        if (usable) {
            chosen[index] = true;
        }
    }
    if (usable) {
        memcpy(selected, chosen, count * sizeof *selected);
    }
    free(numbers);
    free(chosen);
    return usable;
}

static bool scripted_text(ui *self, const char *prompt, const char *default_value, char **answer) {
    (void)default_value;
    scripted_state *state = state_of(self);
    record(state, "text", prompt);
    const char *given = next_answer(state);
    if (given == NULL) {
        return false;
    }
    *answer = text_duplicate(given);
    return true;
}

static bool scripted_confirm(ui *self, const char *question, bool default_answer, bool *answer) {
    (void)default_answer;
    scripted_state *state = state_of(self);
    record(state, "confirm", question);
    const char *given = next_answer(state);
    if (given == NULL) {
        return false;
    }
    if (strcmp(given, "yes") != 0 && strcmp(given, "no") != 0) {
        return unusable(state, given);
    }
    *answer = strcmp(given, "yes") == 0;
    return true;
}

static void scripted_progress(ui *self, const char *label, size_t done, size_t total) {
    char *text = text_format("%s %zu/%zu", label, done, total);
    record(state_of(self), "progress", text);
    free(text);
}

static void scripted_log(ui *self, ui_level level, const char *message) {
    const char *prefix = level == UI_ERROR ? "error: " : level == UI_WARNING ? "warning: " : "";
    char *text = text_format("%s%s", prefix, message);
    record(state_of(self), "log", text);
    free(text);
}

static void scripted_destroy(ui *self) {
    scripted_state *state = state_of(self);
    string_list_free(&state->answers);
    free(state->transcript);
    free(state);
    free(self);
}

static const ui_operations SCRIPTED_OPERATIONS = {
    .menu = scripted_menu,
    .choose_one = scripted_choose_one,
    .choose_many = scripted_choose_many,
    .text = scripted_text,
    .confirm = scripted_confirm,
    .progress = scripted_progress,
    .log = scripted_log,
    .destroy = scripted_destroy,
};

ui *ui_scripted_create(const char *const *answers, size_t count) {
    scripted_state *state = memory_allocate(sizeof *state);
    string_list_init(&state->answers);
    for (size_t index = 0; index < count; index++) {
        string_list_add(&state->answers, answers[index]);
    }
    state->next = 0;
    state->transcript = text_duplicate("");
    state->exhausted = false;
    ui *created = memory_allocate(sizeof *created);
    created->operations = &SCRIPTED_OPERATIONS;
    created->state = state;
    return created;
}

const char *ui_scripted_transcript(const ui *scripted) {
    return state_of(scripted)->transcript;
}

bool ui_scripted_exhausted(const ui *scripted) {
    return state_of(scripted)->exhausted;
}
