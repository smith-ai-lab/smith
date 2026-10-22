// Tests of <smith/contract.h>, in smith's own test library; tests/contract.sh builds them in a scratch project as
// tests/test_contract.c. A failed contract stops the program as an assert does, which smith_test_expect_abort expects.
#include <smith/contract.h>
#include <smith/test.h>

#include <stdint.h>

static int32_t test_contract_clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    const int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);
    return result;
}

static int32_t test_contract_wrong(int32_t x) {
    const int32_t result = x + 1;
    smith_ensures(result == x);
    return result;
}

static int32_t test_contract_sum(int32_t count) {
    int32_t total = 0;
    for (int32_t i = 0; i < count; i++) {
        smith_invariant(total == i * 2);
        total += 2;
        if (i == 2) total += 1; // breaks the invariant on the next pass
    }
    return total;
}

void test_contract_true_contracts_pass(void) {
    SMITH_CHECK(test_contract_clamp(5, 1, 3), ==, 3);
    SMITH_CHECK(test_contract_clamp(-5, 1, 3), ==, 1);
    SMITH_CHECK(test_contract_clamp(2, 1, 3), ==, 2);
    SMITH_CHECK(test_contract_sum(2), ==, 4);
}

void test_contract_a_false_requires_stops(void) {
    smith_test_expect_abort();
    (void)test_contract_clamp(0, 3, 1);
}

void test_contract_a_false_ensures_stops(void) {
    smith_test_expect_abort();
    (void)test_contract_wrong(1);
}

void test_contract_a_false_invariant_stops(void) {
    smith_test_expect_abort();
    (void)test_contract_sum(5);
}
