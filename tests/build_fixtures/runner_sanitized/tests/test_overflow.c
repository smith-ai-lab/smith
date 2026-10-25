#include <smith/test.h>

#include <stdlib.h>

static int read_past(const int *values, int count) {
    return values[count];
}

void test_overflow(void) {
    int *values = malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++)
        values[i] = i;
    volatile int sum = read_past(values, 4);
    (void)sum;
    free(values);
}

void test_signed_overflow(void) {
    volatile int big = 2147483647;
    volatile int more = big + 1;
    (void)more;
}
