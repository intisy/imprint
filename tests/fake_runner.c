#include "fake_runner.h"

#include <stdlib.h>
#include <string.h>

typedef struct fake_rule {
    char *prefix;
    int status;
    char *output;
    int uses_left;
} fake_rule;

typedef struct fake_state {
    string_list commands;
    fake_rule *rules;
    size_t rule_count;
} fake_state;

static const fake_rule *matching_rule(fake_state *state, const char *line) {
    for (size_t index = state->rule_count; index > 0; index--) {
        fake_rule *rule = &state->rules[index - 1];
        if (rule->uses_left != 0 && strncmp(line, rule->prefix, strlen(rule->prefix)) == 0) {
            rule->uses_left -= rule->uses_left > 0 ? 1 : 0;
            return rule;
        }
    }
    return NULL;
}

static int fake_capture(command_runner *self, const char *const *arguments, char **output, error_list *errors) {
    (void)errors;
    fake_state *state = self->state;
    char *line = command_line_text(arguments);
    string_list_add(&state->commands, line);
    const fake_rule *rule = matching_rule(state, line);
    free(line);
    *output = text_duplicate(rule == NULL ? "" : rule->output);
    return rule == NULL ? 0 : rule->status;
}

static int fake_run(command_runner *self, const char *const *arguments, error_list *errors) {
    char *output = NULL;
    int status = fake_capture(self, arguments, &output, errors);
    free(output);
    return status;
}

command_runner fake_runner_create(FILE *log) {
    fake_state *state = memory_allocate(sizeof *state);
    memset(state, 0, sizeof *state);
    string_list_init(&state->commands);
    command_runner runner = { .run = fake_run, .capture = fake_capture, .log = log, .state = state };
    return runner;
}

void fake_runner_answer(command_runner *runner, const char *command_prefix, int status, const char *output) {
    fake_runner_answer_times(runner, command_prefix, status, output, -1);
}

void fake_runner_answer_times(command_runner *runner, const char *command_prefix, int status, const char *output,
                              int times) {
    fake_state *state = runner->state;
    state->rules = memory_reallocate(state->rules, (state->rule_count + 1) * sizeof *state->rules);
    state->rules[state->rule_count].prefix = text_duplicate(command_prefix);
    state->rules[state->rule_count].status = status;
    state->rules[state->rule_count].output = text_duplicate(output == NULL ? "" : output);
    state->rules[state->rule_count].uses_left = times;
    state->rule_count++;
}

const string_list *fake_runner_commands(const command_runner *runner) {
    return &((const fake_state *)runner->state)->commands;
}

bool fake_runner_ran(const command_runner *runner, const char *command_line) {
    return string_list_contains(fake_runner_commands(runner), command_line);
}

long fake_runner_index(const command_runner *runner, const char *command_prefix) {
    const string_list *commands = fake_runner_commands(runner);
    for (size_t index = 0; index < commands->count; index++) {
        if (strncmp(commands->items[index], command_prefix, strlen(command_prefix)) == 0) {
            return (long)index;
        }
    }
    return -1;
}

size_t fake_runner_count(const command_runner *runner, const char *command_prefix) {
    const string_list *commands = fake_runner_commands(runner);
    size_t count = 0;
    for (size_t index = 0; index < commands->count; index++) {
        count += strncmp(commands->items[index], command_prefix, strlen(command_prefix)) == 0 ? 1 : 0;
    }
    return count;
}

void fake_runner_free(command_runner *runner) {
    fake_state *state = runner->state;
    string_list_free(&state->commands);
    for (size_t index = 0; index < state->rule_count; index++) {
        free(state->rules[index].prefix);
        free(state->rules[index].output);
    }
    free(state->rules);
    free(state);
    runner->state = NULL;
}
