#include "platform.h"

#include "text.h"

#include <stdlib.h>

char *platform_read_stream(FILE *stream) {
    size_t capacity = 1024;
    size_t length = 0;
    char *buffer = memory_allocate(capacity);
    size_t read = 0;
    while ((read = fread(buffer + length, 1, capacity - length - 1, stream)) > 0) {
        length += read;
        if (length + 1 == capacity) {
            capacity *= 2;
            buffer = memory_reallocate(buffer, capacity);
        }
    }
    if (ferror(stream) != 0) {
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}
