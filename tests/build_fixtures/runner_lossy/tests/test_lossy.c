#include <smith/test.h>

void test_lossy(void) {
    int count = 3;
    smith_check_equal_i32(count, 3.5);
}
