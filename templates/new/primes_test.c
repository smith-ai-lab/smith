// The tests of @NAME@, run by `smith test`. A test is a function named test_<name>, defined as below.
#include "primes.h"

#include <smith/test.h>

void test_the_first_five_primes_add_up_to_28(void) {
    smith_check_equal_i32(primes_sum(5), 28);
}

void test_no_primes_add_up_to_nothing(void) {
    smith_check_equal_i32(primes_sum(0), 0);
}
