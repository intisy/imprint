#ifndef IMPRINT_ENGINE_HOOKS_H
#define IMPRINT_ENGINE_HOOKS_H

#include <stdio.h>

/** A hook's own exit status becomes the exit code, so the caller sees which way it failed. */
int engine_hooks_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output);

#endif
