#include <smith/test.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void test_profile_abort(void) {
    smith_test_expect_abort();
    abort();
}

void test_profile_trap(void) {
    smith_test_expect_abort();
    __builtin_trap();
}

void test_profile_assert(void) {
    smith_test_expect_abort();
    assert(false);
}

void test_profile_returns(void) {
    smith_test_expect_abort();
}

void test_profile_before(void) {
    abort();
    smith_test_expect_abort();
}

void test_profile_segv(void) {
    smith_test_expect_abort();
    raise(SIGSEGV);
}
