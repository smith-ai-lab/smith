// ASan still reports through the handlers runtime (and with its puts check off): no argument overflows a malloc'd
// block, one argument reads it after free, two make puts read past an unterminated string. tests/handlers_test.smith
// expects a report.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    (void)argv;
    volatile char *block = malloc(8);
    if (argc == 1) block[8] = 1; // heap-buffer-overflow
    free((void *)block);
    if (argc == 2) return block[0]; // heap-use-after-free
    if (argc == 3) {
        char *text = malloc(4);
        memcpy(text, "abcd", 4); // no terminator
        puts(text);              // reads past the block
    }
    return 0;
}
