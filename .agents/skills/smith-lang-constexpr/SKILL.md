---
name: smith-lang-constexpr
description: "Ordinary C functions run at compile time in a .smith file: constexpr i32 x = fib(10);, a lookup table built by a function, static_assert on a constexpr, case labels, array bounds and static initializers, the step limit, the depth limit and undefined behaviour as errors. Load when a .smith file computes a constant with a function, or on 'must be initialized by a constant expression', 'constexpr evaluation exceeded maximum depth', 'exceeded maximum steps' or 'in call to'."
---

# Functions at compile time

C needs a macro, a generator script or a run-time table for a computed constant. In a `.smith` file a plain C function (a definition in the file, any C: loops, locals, structs, recursion, assignments) runs inside the compiler wherever C **requires** a constant:

```smith
module tables;

static i32 fib(i32 n) {                         // plain C: recursion, loops, structs, assignments
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

struct Table {
    i32 v[8];
    i32 n;
};

static struct Table squares(i32 n) {
    struct Table t = {};
    for (i32 i = 0; i < n; i++) {
        t.v[i] = i * i;
        t.n += 1;
    }
    return t;
}

constexpr i32 x = fib(10);                      // 55: run inside the compiler
constexpr struct Table table = squares(6);      // a lookup table, baked into the binary
static_assert(x == 55);                         // a failed check is a compile error

export i32 demo() {
    return x + table.v[5] + table.n;            // 55 + 25 + 6
}
```

The same holds for case labels, array bounds, enum values, bit-field widths and static initializers. In a `.c` file, `constexpr i32 x = fib(10);` is still an error. A call written directly in `static_assert(fib(10) == 55)`, an enum value or a case label builds under the conventions' `-Wpedantic -Werror`.

## Rules

- **The engine is clang's own constant evaluator**, so every answer is the target's: `sizeof(long)` in an evaluated function is 4 for `--target=x86_64-pc-windows-msvc` and 8 for the host. With `-fmodules` it runs the same.
- **Only where C requires a constant** (`constexpr` initializers, `static_assert`, case labels, enum values, bit-field widths, static-storage initializers). Elsewhere the file means what C means: `__builtin_constant_p(f(1))` is 0, `int a[f()]` in a function is a variable-length array, an automatic variable's initializer runs at run time.
- **A constexpr pointer may point into a string literal or another `constexpr` object** (`constexpr const char *s = "abc";`, a struct `{ ptr, len }`, `&hello[1]`, `nums + 2`); C23 requires null, so a `.c` file still rejects it. A pointer to an automatic object is an error; a `static constexpr` one is fine; reading past the object is an error.
- A function of another module runs too, and a failed contract is a compile error at the outermost call: `smith-lang-constexpr-modules`.

## Errors

``constexpr variable 'x' must be initialized by a constant expression``, then the reason and an `in call to 'f(1)'` chain. Reasons: the step budget (`-fconstexpr-steps`, 1048576 by default: a loop that does not end), the call depth (`-fconstexpr-depth`, 512), signed overflow, a read outside an array, a read of a variable that is not constant, a call to a function with no definition in the file (`printf`). Long computations are bounded by the step budget.

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Functions at compile time"); needs smith's LLVM `23.1.3-r7`.
