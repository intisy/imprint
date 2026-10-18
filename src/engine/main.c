#include "engine.h"

#include <stdio.h>

int main(int argument_count, char **arguments) {
    return engine_run(argument_count, (const char *const *)arguments, stdout, stderr);
}
