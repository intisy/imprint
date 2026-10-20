#ifndef IMPRINT_ENGINE_H
#define IMPRINT_ENGINE_H

#include <stdio.h>

extern const char ENGINE_USAGE[];

int engine_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output);

#endif
