#include <stdio.h>

int util_twice(int value) {
    return value * 2;
}

void util_note(void) {
    fprintf(stderr, "note\n");
}
