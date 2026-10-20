#ifndef IMPRINT_ENGINE_INSTALL_H
#define IMPRINT_ENGINE_INSTALL_H

#include <stdio.h>

/** imprint-engine install <resolved-setup.json> [--data DIR] [--stick DIR] [--personal DIR] [--disk DEV] ... */
int engine_install_run(int argument_count, const char *const *arguments, FILE *output, FILE *error_output);

#endif
