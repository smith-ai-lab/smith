#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void raw(const char *path) {
    FILE *file = fopen(path, "r");
    (void)file;
    printf("hi\n");
    (void)getenv("HOME");
    char *memory = malloc(4);
    free(memory);
    exit(1);
}

void pure(const char *text, char *out, int *array, size_t count) {
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%d", 1);
    memcpy(out, text, strlen(text));
    (void)sqrt(2.0);
    (void)strtol(text, nullptr, 10);
    (void)strcmp(text, out);
    qsort(array, count, sizeof array[0], nullptr);
    (void)__builtin_popcount(3u);
    int a = 0;
    (void)sscanf(text, "%d", &a);
}

void failure(const char *text) {
    assert(text != nullptr);
    if (text[0] == 'a') abort();
    if (text[0] == 'b') __builtin_trap();
    if (text[0] == 'c') __builtin_unreachable();
}
