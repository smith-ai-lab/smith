// main has asserts and calls the io library, whose exports are marked [[clang::annotate("smith::edge")]]: a function
// with no body that is the edge of the program, assumed to touch only what its pointers point to and to free nothing.
// The first main is proved from the contract of sum; the second asserts what the contract does not give.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    int32_t lines;
} Console;

[[clang::annotate("smith::edge")]] int32_t console_print(Console *console, int32_t value);

int32_t sum(int32_t count) {
    smith_requires(0 <= count && count <= 5);
    int32_t total = count * 5;
    smith_ensures(total <= 25);
    return total;
}

int32_t run_proved(Console *console) {
    int32_t total = sum(5);
    smith_assert(total <= 25);
    int32_t shown = console_print(console, total);
    smith_assert(total <= 25);
    (void)shown;
    return 0;
}

int32_t run_unproved(Console *console) {
    int32_t total = sum(5);
    (void)console_print(console, total);
    smith_assert(total <= 24);
    return 0;
}
