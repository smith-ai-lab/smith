// A loop's count: a requires bounds it (the loop is then unrolled that many times), and without one the message names
// the variable and the fix. A loop that divides to zero has no count LLVM can derive; a counter bounded by a constant has.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

uint32_t sum_unbounded(size_t count) {
    smith_requires(count >= 0);
    uint32_t total = 0;
    for (size_t i = 0; i < count; i++)
        total += 1;
    return total;
}

uint32_t sum_bounded(size_t count) {
    smith_requires(count <= 5);
    uint32_t total = 0;
    for (size_t i = 0; i < count; i++)
        total += 1;
    smith_ensures(total <= 5);
    return total;
}

size_t digits_divided(uint64_t value) {
    smith_requires(value >= 0);
    size_t count = 0;
    while (value != 0) {
        value /= 10;
        count++;
    }
    return count;
}

size_t digits_counted(uint64_t value) {
    smith_requires(value >= 0);
    size_t count = 0;
    for (size_t pass = 0; pass < 20; pass++) {
        if (value == 0) break;
        value /= 10;
        count++;
    }
    smith_ensures(count <= 20);
    return count;
}
