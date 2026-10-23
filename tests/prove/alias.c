// Two pointer parameters may be the same object, or overlap, unless the source says otherwise (issue #57): C allows
// int32_t * beside uint32_t *, a struct beside a pointer to its member, and a caller may cast, so nothing is assumed
// about parameters of different types either. A claim that relies on it is refuted.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
} Pair;

typedef struct {
    int32_t x;
} Single;

void same_type(int32_t *a, int32_t *b) {
    smith_requires(*a == 1);
    *b = 2;
    smith_ensures(*a == 1);
}

void signed_unsigned(int32_t *a, uint32_t *b) {
    smith_requires(*a == 1);
    *b = 2;
    smith_ensures(*a == 1);
}

void member(Pair *a, int32_t *b) {
    smith_requires(a->x == 1);
    *b = 2;
    smith_ensures(a->x == 1);
}

void other_struct(Pair *a, Single *b) {
    smith_requires(a->x == 1);
    b->x = 2;
    smith_ensures(a->x == 1);
}

// `restrict` says they are different objects, and that is what is assumed (and nothing more).
void restricted(int32_t *restrict a, int32_t *restrict b) {
    smith_requires(*a == 1);
    *b = 2;
    smith_ensures(*a == 1);
}

void restricted_other_type(Pair *restrict a, int32_t *b) {
    smith_requires(a->x == 1);
    *b = 2;
    smith_ensures(a->x == 1);
}

// Two restrict pointers that are only read are not assumed different (a call may pass one object twice).
int32_t reads_both(const int32_t *restrict a, const int32_t *restrict b) {
    smith_requires(*a == 1 && -100 < *b && *b < 100);
    const int32_t sum = *a + *b;
    smith_ensures(sum == 1 + *b);
    return sum;
}
