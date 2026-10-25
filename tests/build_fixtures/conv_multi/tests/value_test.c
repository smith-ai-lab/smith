#include <smith/test.h>

#include "base/value.h"
#include "tests/helper.h"

void test_value(void) {
    smith_check_equal_i32(base_value(), helper_twenty_one());
}
