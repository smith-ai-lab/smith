#include <smith/test.h>

#include <stdio.h>

void test_c_passing(void) {
    printf("this output is hidden\n");
    smith_check(true);
    smith_check_equal_i32(1 + 1, 2);
    smith_check_near_f64(0.1 + 0.2, 0.3, 1e-9);
    smith_check_same_bits_f32(1.5F, 1.5F);
}

void test_c_equal(void) {
    long long x = 7;
    smith_check_equal_i64(x, 6);
}

void test_c_near(void) {
    smith_check_near_f64(1.0, 1.5, 0.1);
}

void test_c_pointer(void) {
    int value = 0;
    int *pointer = &value;
    smith_check_equal_ptr(pointer, nullptr);
}
