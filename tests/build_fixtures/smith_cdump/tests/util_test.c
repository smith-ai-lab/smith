#include "util.h"

#include <smith/test.h>

void test_twice(void) {
    smith_check(util_twice(2) == 4);
}
