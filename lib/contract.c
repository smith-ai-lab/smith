// <smith/contract.h>'s checks. A failed contract is a failure like a failed check: smith_failure (<smith/trace.h>) says
// which kind failed, records the stack, and stops the program (or, in a test, fails it with the same report as a failed
// check: the failing line and the call chain). They are external functions, so that the call stays in the IR at every
// optimization level, where `smith prove` finds it. A program that uses them links the trace library too
// (smith_library(b, "trace"); smith_tests does it itself).
#include <smith/contract.h>
#include <smith/trace.h>

void smith_contract_requires(bool condition) {
    if (!condition) smith_failure(SmithFailure_Requires);
}

void smith_contract_ensures(bool condition) {
    if (!condition) smith_failure(SmithFailure_Ensures);
}

void smith_contract_invariant(bool condition) {
    if (!condition) smith_failure(SmithFailure_Invariant);
}

// A marker for `smith prove`, which reads the call in the IR: a trusted function is assumed, not proved. It checks
// nothing, at run time or anywhere else.
void smith_contract_trusted(void) {}
