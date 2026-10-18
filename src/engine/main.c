#include <stdio.h>
#include <string.h>

int main(int argument_count, char **arguments) {
    if (argument_count == 2 && strcmp(arguments[1], "--version") == 0) {
        printf("imprint-engine %s\n", IMPRINT_VERSION);
        return 0;
    }
    fputs("usage: imprint-engine --version\n", stderr);
    return 2;
}
