// <smith/contract.h>: contracts written like assertions. Each macro is a check that prints the expression's text, the
// file and the line, then stops the program, as `assert` does, and `smith prove` proves the same statements on LLVM IR.
//
//   int32_t clamp(int32_t x, int32_t low, int32_t high) {
//       SMITH_REQUIRES(low <= high);                     // the caller's job
//       int32_t result = x < low ? low : x > high ? high : x;
//       SMITH_ENSURES(low <= result && result <= high);  // this function's promise
//       return result;
//   }
//
//   for (uint8_t square = 0; square < squares; square++) {
//       SMITH_INVARIANT(moves->len <= 2 * square);       // the first statement of a loop body
//       ...
//   }
//
// SMITH_REQUIRES(c)   the start of a function: a caller must make it true.
// SMITH_ENSURES(c)    just before a `return`: the function makes it true. An old value is a `const` local saved at the
//                     top, and the result is the variable about to be returned.
// SMITH_INVARIANT(c)  the first statement of a loop body: true on every pass.
// SMITH_CONTRACT_FOR(Type, spec)
//                     a function pointer type's contract is the body-less spec function `spec`, whose only statements
//                     are contracts. It is declared now and enforced later: today it records the pair and nothing else.
//
// They are macros because only a macro can capture the expression's text, its file and its line. With NDEBUG they
// compile away, as `assert` does: the expression is not evaluated, but it is still type-checked and its variables are
// still used, so an old value kept only for a contract draws no unused-variable warning.
//
// Each macro calls a function of the library (build.c: smith_library(b, "contract")), whatever the optimization level,
// and `smith prove` finds those calls in the IR by name, so no flag is needed to prove a file:
//   void smith_contract_requires(bool condition, const char *text, const char *file, int line);
//   void smith_contract_ensures(bool condition, const char *text, const char *file, int line);
//   void smith_contract_invariant(bool condition, const char *text, const char *file, int line);
// A contract is an ordinary C expression, so a spec can be a helper function with loops of constant bounds.
#pragma once

// What SMITH_CONTRACT_FOR records: the name of the function pointer type and the spec function, as `smith prove` reads
// them from the IR, in the global smith_contract_for_<Type>.
typedef struct {
    const char *type;
    void (*spec)(void);
} SmithContractFor;

// A failed check prints "<file>:<line>: <kind>(<text>) failed" to stderr and calls abort(). condition is only a bool,
// so the checks cost one call each.
void smith_contract_requires(bool condition, const char *text, const char *file, int line);
void smith_contract_ensures(bool condition, const char *text, const char *file, int line);
void smith_contract_invariant(bool condition, const char *text, const char *file, int line);

#ifdef NDEBUG
#define SMITH_REQUIRES(condition) ((void)sizeof(condition))
#define SMITH_ENSURES(condition) ((void)sizeof(condition))
#define SMITH_INVARIANT(condition) ((void)sizeof(condition))
#else
#define SMITH_REQUIRES(condition) smith_contract_requires((condition), #condition, __FILE__, __LINE__)
#define SMITH_ENSURES(condition) smith_contract_ensures((condition), #condition, __FILE__, __LINE__)
#define SMITH_INVARIANT(condition) smith_contract_invariant((condition), #condition, __FILE__, __LINE__)
#endif

// Always present, NDEBUG included: it is data, not a check. `used` keeps it in the object, where the checker looks.
#define SMITH_CONTRACT_FOR(Type, spec) \
    [[gnu::used]] static const SmithContractFor smith_contract_for_##Type = {#Type, (void (*)(void))(spec)}
