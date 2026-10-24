---
name: smith-lang-constexpr
description: "Ordinary C functions run at compile time in a .smith file: constexpr i32 x = fib(10);, static_assert(sum_to(100) == 5050), a table built by a function, case labels, enum values, bit-field widths and static initializers, the step limit, the depth limit and undefined behaviour as errors. Load when a .smith file computes a constant with a function, or on 'must be initialized by a constant expression', 'constexpr evaluation exceeded maximum depth', 'exceeded maximum steps' or 'in call to'."
---

# Functions at compile time

This skill ships in the smith plugin, from `.agents/skills/smith-lang-constexpr/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; the long reference is [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Functions at compile time"). It needs smith's LLVM `23.1.3-r6`.

In a `.smith` file, a plain C function (a definition in the file, any C: loops, locals, structs, recursion, assignments) runs inside the compiler wherever C **requires** a constant:

```smith fragment
static i32 fib(i32 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
struct Table { i32 v[8]; i32 n; };
static struct Table squares(i32 n) {
    struct Table t = {};
    for (i32 i = 0; i < n; i++) { t.v[i] = i * i; t.n += 1; }
    return t;
}

constexpr i32 x = fib(10);               // 55
constexpr struct Table table = squares(6);
static_assert(fib(10) == 55);
enum { Fib7 = fib(7) };                  // also case labels, bit-field widths, static initializers
```

- **The engine is clang's own constant evaluator**, so every answer is the target's: `sizeof(long)` in a function that is evaluated gives 4 for `--target=x86_64-pc-windows-msvc` and 8 for the host.
- **Only where C requires a constant** (`constexpr` initializers, `static_assert`, case labels, enum values, bit-field widths, static-storage initializers). Everywhere else the file means what C means: `__builtin_constant_p(f(1))` is 0, `int a[f()]` in a function is a variable-length array, an automatic variable's initializer runs at run time, and an `asm` `"i"` operand with a call is still an error. In a `.c` file, `constexpr i32 x = fib(10);` is still an error.
- **A function that cannot be evaluated is an error with clang's notes**: `constexpr variable 'x' must be initialized by a constant expression`, then the reason and an `in call to 'f(1)'` chain. Reasons: the step budget (`-fconstexpr-steps`, 1048576 by default, for a loop that does not end), the call depth (`-fconstexpr-depth`, 512), signed overflow, a read outside an array, a read of a variable that is not constant, a call to a function with no definition in the file (`printf`).
- **With clang modules** (`-fmodules`) the evaluator runs the same.
- **A failed contract is a compile error at the outermost call, in the run-time report's form.** `contract::requires`, `ensures`, `invariant`, `assert` and `test::check` inside a compile-time call stop the evaluator; the first note says what failed at the contract's line, then the stack of calls, innermost first, each with file, line and arguments (see `smith-contract`):

```text
main.smith:18:17: error: constexpr variable 'bad' must be initialized by a constant expression
main.smith:10:15: note: requires failed in call to 'contract::requires(false)'
main.smith:18:23: note: in call to 'make_table(0)'
```

- **A constexpr pointer may point into a string literal or into another `constexpr` object** (`constexpr const char *s = "abc";`, a struct `{ ptr, len }`, `&hello[1]`, `nums + 2`); C23 requires null, so a `.c` file still rejects it. A pointer to an automatic object is an error; a `static constexpr` one is fine; reading past the object is an error.
- **A function of another module runs too (issue #107).** Any exported function of an import, with the private functions, types and constants of its module that it uses and the exports of the modules that module imports, runs where C requires a constant, in `static_assert`, `constexpr` initializers, enum values, case labels, array bounds, an `if constexpr` condition and a generic's computed result type (`export shapes::slice_of(T, counts::dims(2, 3)) make(...)`). Nothing is written in the importer: the module's own compile wrote the function's lowered text to a `.body` file beside its header, and the importer defines what it mentions after its last `import` line, as copies that emit no code (the module's symbol stays the function). Imports of imports work. Limits: a function that reads or writes a variable of its module that is not `constexpr` is not carried (a copy would be a second variable; the call is the error above), and one whose evaluation calls a generic's instance is not carried yet. A call at run time asks for nothing. **Not here yet**: long computations are bounded by the step budget.
