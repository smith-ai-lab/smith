#include <smith/test.h>

static int twice(int x) {
    int doubled = x * 2;
    return doubled;
}

void test_twice_doubles(void) {
    int result = twice(21);
    smith_check_equal_i32(result, 42);
}
