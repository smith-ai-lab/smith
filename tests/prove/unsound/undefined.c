// False claims about undefined behaviour: an overflow, a shift, a division, an access outside the object, a read of
// what was never written. UBSan, ASan and -ftrivial-auto-var-init refute each at run time.
// unsound: add_overflows runtime error
// unsound: negate_min runtime error
// unsound: shift_too_far runtime error
// unsound: divide_by_zero runtime error
// unsound: index_one_past ensures failed
// unsound: array_one_past AddressSanitizer
// unsound: sum_past_one_object AddressSanitizer
// unsound: read_uninitialized ensures failed
// unsound: read_uninitialized_branch ensures failed
// unsound: read_uninitialized_member ensures failed
// unsound: read_uninitialized_element ensures failed
#include <smith/contract.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int32_t add_overflows(int32_t a, int32_t b) {
    smith_requires(a >= 0);
    int32_t sum = a + b;
    smith_ensures(sum >= a);
    return sum;
}

int32_t negate_min(int32_t x) {
    int32_t result = x < 0 ? -x : x;
    smith_ensures(result >= 0);
    return result;
}

uint32_t shift_too_far(uint32_t x, uint32_t by) {
    smith_requires(by <= 32);
    uint32_t result = x << by;
    smith_ensures(result >= 0);
    return result;
}

int32_t divide_by_zero(int32_t a, int32_t b) {
    smith_requires(b >= 0);
    int32_t result = a / b;
    smith_ensures(result <= a || a < 0);
    return result;
}

typedef struct {
    int32_t v[4];
} Four;

// A fixed array indexed by its own length.
int32_t array_one_past(const Four *four, size_t i) {
    smith_requires(i <= 4);
    int32_t value = four->v[i];
    smith_ensures(value >= 0);
    return value;
}

// A pointer parameter is one object of its type, so reading the next int is outside it.
int32_t sum_past_one_object(const int32_t *p, size_t n) {
    smith_requires(n <= 3);
    int32_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += p[i] & 7;
    smith_ensures(total >= 0);
    return total;
}

// Written on one path only.
int32_t read_uninitialized(int32_t flag) {
    int32_t x;
    if (flag != 0) x = 7;
    smith_ensures(x == 7);
    return x;
}

int32_t read_uninitialized_branch(int32_t flag) {
    smith_requires(flag == 0);
    int32_t x;
    if (flag != 0) x = 7;
    int32_t result = x == 7 ? 1 : 0;
    smith_ensures(result == 1);
    return result;
}

typedef struct {
    int32_t a;
    int32_t b[3];
} Mixed;

// A struct written on one path only, one member and one element.
int32_t read_uninitialized_member(int32_t flag) {
    Mixed m;
    if (flag != 0) {
        m.a = 1;
        m.b[1] = 2;
    }
    smith_ensures(m.a == 1);
    return m.a;
}

int32_t read_uninitialized_element(int32_t flag) {
    Mixed m;
    if (flag != 0) {
        m.a = 1;
        m.b[1] = 2;
    }
    smith_ensures(m.b[1] == 2);
    return m.b[1];
}

// A claim about an index that is one past what the requires allows.
int32_t index_one_past(int32_t x) {
    smith_requires(x >= 0);
    smith_ensures(x <= 4);
    return x;
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *name = argv[1];
    Four four = {{1, 2, 3, 4}};
    int32_t one = 1;
    if (strcmp(name, "add_overflows") == 0) (void)add_overflows(1, INT32_MAX);
    if (strcmp(name, "negate_min") == 0) (void)negate_min(INT32_MIN);
    if (strcmp(name, "shift_too_far") == 0) (void)shift_too_far(1, 32);
    if (strcmp(name, "divide_by_zero") == 0) (void)divide_by_zero(1, 0);
    if (strcmp(name, "index_one_past") == 0) (void)index_one_past(5);
    if (strcmp(name, "array_one_past") == 0) (void)array_one_past(&four, 4);
    if (strcmp(name, "sum_past_one_object") == 0) (void)sum_past_one_object(&one, 3);
    if (strcmp(name, "read_uninitialized") == 0) (void)read_uninitialized(0);
    if (strcmp(name, "read_uninitialized_branch") == 0) (void)read_uninitialized_branch(0);
    if (strcmp(name, "read_uninitialized_member") == 0) (void)read_uninitialized_member(0);
    if (strcmp(name, "read_uninitialized_element") == 0) (void)read_uninitialized_element(0);
    return 0;
}
