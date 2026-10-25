#include <smith/contract.h>
#include <smith/test.h>

static int halve(int value) {
    smith_requires(value % 2 == 0);
    return value / 2;
}

void test_requires_fails(void) {
    int odd = 7;
    (void)halve(odd);
}

void test_requires_aborts(void) {
    smith_test_expect_abort();
    (void)halve(5);
}

void test_requires_holds(void) {
    smith_check_equal_i32(halve(8), 4);
}
