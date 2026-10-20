#ifndef IMPRINT_UI_H
#define IMPRINT_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum ui_level { UI_INFORMATION, UI_WARNING, UI_ERROR } ui_level;

typedef struct ui ui;

typedef struct ui_operations {
    bool (*menu)(ui *self, const char *title, const char *const *items, size_t count, size_t *chosen);
    bool (*choose_one)(ui *self, const char *prompt, const char *const *items, size_t count, size_t preselected,
                       size_t *chosen);
    bool (*choose_many)(ui *self, const char *prompt, const char *const *items, size_t count, bool *selected);
    bool (*text)(ui *self, const char *prompt, const char *default_value, char **answer);
    bool (*secret)(ui *self, const char *prompt, char **answer);
    bool (*confirm)(ui *self, const char *question, bool default_answer, bool *answer);
    void (*progress)(ui *self, const char *label, size_t done, size_t total);
    void (*log)(ui *self, ui_level level, const char *message);
    void (*destroy)(ui *self);
} ui_operations;

struct ui {
    const ui_operations *operations;
    void *state;
};

static inline bool ui_menu(ui *self, const char *title, const char *const *items, size_t count, size_t *chosen) {
    return self->operations->menu(self, title, items, count, chosen);
}

static inline bool ui_choose_one(ui *self, const char *prompt, const char *const *items, size_t count,
                                 size_t preselected, size_t *chosen) {
    return self->operations->choose_one(self, prompt, items, count, preselected, chosen);
}

static inline bool ui_choose_many(ui *self, const char *prompt, const char *const *items, size_t count,
                                  bool *selected) {
    return self->operations->choose_many(self, prompt, items, count, selected);
}

static inline bool ui_text(ui *self, const char *prompt, const char *default_value, char **answer) {
    return self->operations->text(self, prompt, default_value, answer);
}

/** A text answer that is not shown while it is typed and never recorded. */
static inline bool ui_secret(ui *self, const char *prompt, char **answer) {
    return self->operations->secret(self, prompt, answer);
}

static inline bool ui_confirm(ui *self, const char *question, bool default_answer, bool *answer) {
    return self->operations->confirm(self, question, default_answer, answer);
}

static inline void ui_progress(ui *self, const char *label, size_t done, size_t total) {
    self->operations->progress(self, label, done, total);
}

static inline void ui_log(ui *self, ui_level level, const char *message) {
    self->operations->log(self, level, message);
}

static inline void ui_destroy(ui *self) {
    self->operations->destroy(self);
}

ui *ui_ansi_create(FILE *input, FILE *output, bool colors);
bool ui_ansi_colors_available(FILE *output);

ui *ui_scripted_create(const char *const *answers, size_t count);
const char *ui_scripted_transcript(const ui *scripted);
bool ui_scripted_exhausted(const ui *scripted);

#endif
