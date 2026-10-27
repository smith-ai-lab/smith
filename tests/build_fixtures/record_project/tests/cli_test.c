#include <smith/test.h>

#include <stdlib.h>

void test_add_runs(void) {
    smith_check_equal_i32(system("build/debug/calc add 1 2 > /dev/null"), 0);
}

void test_mul_runs(void) {
    smith_check_equal_i32(system("build/debug/calc mul 3 4 > /dev/null"), 0);
}
