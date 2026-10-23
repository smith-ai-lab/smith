// `restrict` is C's promise that two pointers are different objects, and the prover takes it as one without checking it
// at the call (issue #65): a caller that breaks it makes the proof's claim false. Until #65 these are proved, so the
// markers say unsound-until-65: smith prove says ok, the run refutes it, and the day #65 makes the prover refuse (or
// check the call) the script fails and these lines become `unsound:`.
// unsound-until-65: restricted_twice ensures failed
// unsound-until-65: caller_breaks_restrict ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

void restricted_twice(int32_t *restrict a, int32_t *restrict b) {
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
    if (strcmp(argv[1], "restricted_twice") == 0) restricted_twice(&x, &x);
    if (strcmp(argv[1], "caller_breaks_restrict") == 0) caller_breaks_restrict(&x);
    return 0;
}
