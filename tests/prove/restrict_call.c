// `restrict` is checked at the call (issue #65): a function called with two objects that may be the same, or overlap,
// through a `restrict` parameter and another pointer is a failure at the call, naming both. A pointer to a type is
// one object of it; a function that uses a `restrict` pointer as an array has no range the prover knows, and the
// call is an error at its declaration.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
} Pair;

static void set_both(Pair *restrict a, Pair *restrict b) {
    a->x = 1;
    b->x = 2;
}

static void set_member(Pair *restrict pair, int32_t *restrict member) {
    pair->x = 1;
    *member = 2;
}

// Two locals, two elements of an array, and a pair and a field it does not own: apart.
void apart_locals(void) {
    smith_ensures(true);
    Pair a = {0, 0};
    Pair b = {0, 0};
    set_both(&a, &b);
}

void apart_elements(Pair pairs[static 2]) {
    smith_requires(true);
    set_both(&pairs[0], &pairs[1]);
    smith_ensures(true);
}

void apart_member(Pair *restrict pair, int32_t *restrict other) {
    smith_requires(true);
    set_member(pair, other);
    smith_ensures(true);
}

// The same object twice.
void same_object(Pair *pair) {
    smith_requires(true);
    set_both(pair, pair);
    smith_ensures(true);
}

// A pointer and a pointer to its member overlap.
void overlapping(Pair *pair) {
    smith_requires(true);
    set_member(pair, &pair->y);
    smith_ensures(true);
}

// Two pointers nothing says are different.
void unknown(Pair *a, Pair *b) {
    smith_requires(true);
    set_both(a, b);
    smith_ensures(true);
}

// Read only by both: allowed to be the same.
static int32_t sum(const Pair *restrict a, const Pair *restrict b) { return a->x + b->x; }

int32_t reads(const Pair *pair) {
    smith_requires(-100 < pair->x && pair->x < 100);
    const int32_t total = sum(pair, pair);
    smith_ensures(total == 2 * pair->x);
    return total;
}

// A `restrict` pointer used as an array: its range is not known.
static void copy(uint8_t *restrict dst, const uint8_t *restrict src, size_t n) {
    for (size_t i = 0; i < n && i < 4; i++)
        dst[i] = src[i];
}

void copies(void) {
    smith_ensures(true);
    uint8_t a[4] = {0};
    uint8_t b[4] = {0};
    copy(a, b, 4);
}

// A trusted function's calls are not checked.
static void trusted_set(Pair *restrict a, Pair *restrict b) {
    smith_trusted();
    smith_requires(true);
    a->x = 1;
    b->x = 2;
    smith_ensures(true);
}

void trusted_twice(Pair *pair) {
    smith_requires(true);
    trusted_set(pair, pair);
    smith_ensures(true);
}
