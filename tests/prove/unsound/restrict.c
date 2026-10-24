// `restrict` is C's promise that two pointers are different objects, and the prover assumes it in the callee and checks it
// at each call of a proved function (issue #65): a caller that breaks it fails at the call. The callee's own proof is
// the promise's, so a direct call from outside the proof (main below) is what the run refutes.
// unsound: caller_breaks_restrict ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

static void restricted_twice(int32_t *restrict a, int32_t *restrict b) {
    smith_requires(*a == 1);
    *b = 2;
    smith_ensures(*a == 1);
}

// The call is where the promise is broken: the same object is both a and b.
void caller_breaks_restrict(int32_t *p) {
    smith_requires(*p == 1);
    restricted_twice(p, p);
    smith_ensures(*p == 1);
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    int32_t x = 1;
    if (strcmp(argv[1], "caller_breaks_restrict") == 0) caller_breaks_restrict(&x);
    return 0;
}
