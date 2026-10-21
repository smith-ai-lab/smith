// <smith/contract.h>'s checks: a failed contract prints its text, file and line, and stops the program. They are
// functions, not inline code, so that the call stays in the IR at every optimization level, where `smith prove` finds
// it.
#include <smith/contract.h>

#include <stdio.h>
#include <stdlib.h>

[[noreturn]] static void smith_contract_fail(const char *kind, const char *text, const char *file, int line) {
    (void)fprintf(stderr, "%s:%d: %s(%s) failed\n", file, line, kind, text);
    abort();
}

void smith_contract_requires(bool condition, const char *text, const char *file, int line) {
    if (!condition) smith_contract_fail("SMITH_REQUIRES", text, file, line);
}

void smith_contract_ensures(bool condition, const char *text, const char *file, int line) {
    if (!condition) smith_contract_fail("SMITH_ENSURES", text, file, line);
}

void smith_contract_invariant(bool condition, const char *text, const char *file, int line) {
    if (!condition) smith_contract_fail("SMITH_INVARIANT", text, file, line);
}
