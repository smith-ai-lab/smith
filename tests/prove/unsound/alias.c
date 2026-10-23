// False claims that rest on two pointer parameters being different objects (issue #57): the prover must not assume it
// unless `restrict` says so. Each is refuted at run time by an input that passes the same object twice.
// unsound: same_type ensures failed
// unsound: signed_unsigned ensures failed
// unsound: member ensures failed
// unsound: other_struct ensures failed
// unsound: through_char ensures failed
// unsound: callee_const_cast_writes ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

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

// A character type may alias anything.
void through_char(int32_t *a, char *c) {
    smith_requires(*a == 1);
    *c = 2;
    smith_ensures(*a == 1);
}

// The callee has a contract and writes through a const pointer (legal C when the object is not const); its caller must
// not rely on a const pointer being untouched.
static void writes_through_const(const int32_t *p) {
    *(int32_t *)p = 5;
    smith_ensures(true);
}

void callee_const_cast_writes(int32_t *p) {
    smith_requires(*p == 1);
    writes_through_const(p);
    smith_ensures(*p == 1);
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    int32_t x = 1;
    Pair pair = {1, 1};
    const char *name = argv[1];
    if (strcmp(name, "same_type") == 0) same_type(&x, &x);
    if (strcmp(name, "signed_unsigned") == 0) signed_unsigned(&x, (uint32_t *)&x);
    if (strcmp(name, "member") == 0) member(&pair, &pair.x);
    if (strcmp(name, "other_struct") == 0) other_struct(&pair, (Single *)&pair);
    if (strcmp(name, "through_char") == 0) through_char(&x, (char *)&x);
    if (strcmp(name, "callee_const_cast_writes") == 0) callee_const_cast_writes(&x);
    return 0;
}
