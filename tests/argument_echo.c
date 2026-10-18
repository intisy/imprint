#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    for (int index = 1; index < argc; index++) {
        printf("[%s]\n", argv[index]);
    }
    return 0;
}
