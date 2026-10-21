// <smith/contract.h>'s checks: a failed contract says which kind failed and stops the program. They are external
// functions, so that the call stays in the IR at every optimization level, where `smith prove` finds it.
#include <smith/contract.h>

#include <stdio.h>
#include <stdlib.h>

[[noreturn]] static void smith_contract_fail(const char *kind) {
    (void)fprintf(stderr, "smith: %s failed\n", kind);
    abort();
}

void smith_contract_requires(bool condition) {
    if (!condition) smith_contract_fail("smith_requires");
}

void smith_contract_ensures(bool condition) {
    if (!condition) smith_contract_fail("smith_ensures");
}

void smith_contract_invariant(bool condition) {
    if (!condition) smith_contract_fail("smith_invariant");
}
