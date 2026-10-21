// <smith/contract.h>: contracts written as calls. Each is a check that stops the program when its condition is false,
// as `assert` does, and `smith prove` proves the same statements on LLVM IR.
//
//   int32_t clamp(int32_t x, int32_t low, int32_t high) {
//       smith_requires(low <= high);                     // the caller's job
//       int32_t result = x < low ? low : x > high ? high : x;
//       smith_ensures(low <= result && result <= high);  // this function's promise
//       return result;
//   }
//
//   for (uint8_t square = 0; square < squares; square++) {
//       smith_invariant(moves->len <= 2 * square);       // the first statement of a loop body
//       ...
//   }
//
// smith_requires(c)   the start of a function: a caller must make c true.
// smith_ensures(c)    just before a `return`: the function makes c true. An old value is a `const` local saved at the
//                     top, and the result is the variable about to be returned.
// smith_invariant(c)  the first statement of a loop body: c is true on every pass.
// SMITH_CONTRACT_FOR(Type, spec)
//                     a function pointer type's contract is the body-less spec function `spec`, whose only statements
//                     are contracts. It declares something rather than checking a value, so it is a macro; today it
//                     records the pair and nothing else.
//
// A condition must be free of side effects: it is evaluated by the check, and not at all with NDEBUG.
//
// They are `static inline` functions that always inline, with an empty body under NDEBUG, so the optimizer removes
// the arguments they leave unused. Without NDEBUG each is one call of a function of the library (build.c:
// smith_library(b, "contract")), at every optimization level, since that function is external and the optimizer cannot
// see into it, which is how `smith prove` finds the contracts in the IR without a flag:
//   void smith_contract_requires(bool condition);
//   void smith_contract_ensures(bool condition);
//   void smith_contract_invariant(bool condition);
// A failed check is smith_failure(kind) (<smith/trace.h>): it records the stack and writes `smith-trace begin requires
// failed` and the frames to standard error, then calls abort(); in a test it fails the test with the same report as a
// failed check, the failing line and the call chain. Where and what come from the stack and the source line, not from
// the library, which links with the trace library (smith_library(b, "trace")).
#pragma once

// What SMITH_CONTRACT_FOR records: the name of the function pointer type and the spec function, as `smith prove` reads
// them from the IR, in the global smith_contract_for_<Type>.
typedef struct {
    const char *type;
    void (*spec)(void);
} SmithContractFor;

void smith_contract_requires(bool condition);
void smith_contract_ensures(bool condition);
void smith_contract_invariant(bool condition);

#ifdef NDEBUG
[[gnu::always_inline]] static inline void smith_requires(bool condition) {
    (void)condition;
}
[[gnu::always_inline]] static inline void smith_ensures(bool condition) {
    (void)condition;
}
[[gnu::always_inline]] static inline void smith_invariant(bool condition) {
    (void)condition;
}
#else
[[gnu::always_inline]] static inline void smith_requires(bool condition) {
    smith_contract_requires(condition);
}
[[gnu::always_inline]] static inline void smith_ensures(bool condition) {
    smith_contract_ensures(condition);
}
[[gnu::always_inline]] static inline void smith_invariant(bool condition) {
    smith_contract_invariant(condition);
}
#endif

// Always present, NDEBUG included: it is data, not a check. `used` keeps it in the object, where the checker looks.
#define SMITH_CONTRACT_FOR(Type, spec) \
    [[gnu::used]] static const SmithContractFor smith_contract_for_##Type = {#Type, (void (*)(void))(spec)}
