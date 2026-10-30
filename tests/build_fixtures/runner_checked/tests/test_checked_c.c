// C that calls plain malloc and printf, for the tests of tests/mem_checked_test.smith: the runner's allocator reaches a
// C library's malloc (issue #815), and the C library's own blocks are not leaks.
#include <stdio.h>
#include <stdlib.h>

void test_checked_7_c_leak(void) {
    volatile char *kept = malloc(16); // c leak site
    kept[0] = 1;
}

void test_checked_8_c_malloc_freed(void) {
    volatile char *block = malloc(16);
    block[0] = 1;
    free((void *)block);
}

void test_checked_9_printf(void) {
    printf("checked: %d %s %f\n", 42, "text", 1.5);
    fflush(stdout);
}
