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
// smith_trusted()    the first statement of a function at the program's edge (io, inline assembly, a library with no
//                     source): `smith prove` does not prove it. Its smith_requires is proved at each call and its
//                     smith_ensures assumed, as for any function with a contract, but nothing in its body is read, and
//                     the report lists it as trusted, never as proved. It checks nothing at run time.
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
//   void smith_contract_trusted(void);
// A failed check is smith_failure(kind) (<smith/trace.h>): it records the stack and writes `smith-trace begin requires
// failed` and the frames to standard error, then calls abort(); in a test it fails the test with the same report as a
// failed check, the failing line and the call chain. Where and what come from the stack and the source line, not from
// the library, which links with the trace library (smith_library(b, "trace")).
#pragma once

void smith_contract_requires(bool condition);
void smith_contract_ensures(bool condition);
void smith_contract_invariant(bool condition);
void smith_contract_trusted(void);

// clang's static analyzer (clang-tidy's clang-analyzer-*, `smith lint`) cannot see what a call means, so without help
// it reports every index a contract bounds. Under __clang_analyzer__ (defined only there) each is an assumption, as the
// failure path of `assert` is for it, and nothing else changes: a compile, `smith prove` and the checks are as above.
#ifdef __clang_analyzer__
[[gnu::always_inline]] static inline void smith_requires(bool condition) {
    __builtin_assume(condition);
}
[[gnu::always_inline]] static inline void smith_ensures(bool condition) {
    __builtin_assume(condition);
}
[[gnu::always_inline]] static inline void smith_invariant(bool condition) {
    __builtin_assume(condition);
}
[[gnu::always_inline]] static inline void smith_trusted(void) {}
#elifdef NDEBUG
[[gnu::always_inline]] static inline void smith_requires(bool condition) {
    (void)condition;
}
[[gnu::always_inline]] static inline void smith_ensures(bool condition) {
    (void)condition;
}
[[gnu::always_inline]] static inline void smith_invariant(bool condition) {
    (void)condition;
}
[[gnu::always_inline]] static inline void smith_trusted(void) {}
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
[[gnu::always_inline]] static inline void smith_trusted(void) {
    smith_contract_trusted();
}
#endif
