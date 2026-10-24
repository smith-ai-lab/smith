// A loop with smith_invariant is proved by induction: the invariant holds on entry, and one pass of the body from an
// arbitrary state that satisfies it keeps it, whatever the bound. A loop of n <= 4096 costs one pass, not 4096.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

// An inductive invariant, and a bound that unrolling would pay for 4096 times.
uint32_t triple(uint32_t n) {
    smith_requires(n <= 4096);
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        smith_invariant(i <= n && total == 3 * i);
        total += 3;
    }
    smith_ensures(total == 3 * n);
    return total;
}

// No bound at all: the loop counts down from any n, and the invariant carries the proof.
int32_t countdown(int32_t n) {
    smith_requires(0 <= n);
    int32_t left = n;
    int32_t taken = 0;
    while (left > 0) {
        smith_invariant(0 <= left && left + taken == n);
        left--;
        taken++;
    }
    smith_ensures(taken == n);
    return taken;
}

// Not inductive: `total <= 10` holds after 3 passes and not after 4, so a pass from total == 9 breaks it.
uint32_t too_small(uint32_t n) {
    smith_requires(n <= 4096);
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        smith_invariant(total <= 10);
        total += 3;
    }
    return total;
}

// True of every pass but the first: it does not hold on entry.
uint32_t not_on_entry(uint32_t n) {
    smith_requires(n <= 4096);
    uint32_t total = 5;
    for (uint32_t i = 0; i < n; i++) {
        smith_invariant(total == 3 * i);
        total += 3;
    }
    return total;
}

// A local array the loop writes: it is arbitrary at the start of a pass, and the index is in range from the bound.
uint32_t filled(uint32_t n) {
    smith_requires(n <= 64);
    uint32_t cells[64] = {0};
    uint32_t written = 0;
    for (uint32_t i = 0; i < n; i++) {
        smith_invariant(i <= n && written == i);
        cells[i] = 7;
        written++;
    }
    smith_ensures(written == n);
    return written;
}
