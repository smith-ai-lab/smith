// A smith_requires that bounds a loop's passes (the unrolling reads it as a range of the parameter) is also a premise of
// the rest of the function: the index after the loop is in range because of it, with no second contract. The false
// variant reads one past the table and is refuted. `last_of` is also held by the unroller's own exit; `early` leaves the
// loop by a break, so only the premise says n <= 8.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

uint32_t last_of(size_t n) {
    smith_requires(n >= 1 && n <= 8);
    uint32_t table[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += table[i];
    return total + table[n - 1];
}

uint32_t one_past(size_t n) {
    smith_requires(n >= 1 && n <= 8);
    uint32_t table[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += table[i];
    return total + table[n];
}

uint32_t early(size_t n, uint32_t key) {
    smith_requires(n >= 1 && n <= 8);
    uint32_t table[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (table[i] == key) break;
        total += 1;
    }
    return total + table[n - 1];
}

uint32_t early_past(size_t n, uint32_t key) {
    smith_requires(n >= 1 && n <= 8);
    uint32_t table[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (table[i] == key) break;
        total += 1;
    }
    return total + table[n];
}
