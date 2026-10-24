---
name: smith-lang-constexpr-modules
description: "Compile-time calls across modules and failed contracts: an exported function of an import runs where C requires a constant (static_assert, constexpr, array bounds, if constexpr, a generic's result type), the .body file, its limits, and a failed contract::requires inside a compile-time call as a compile error with the call stack. Load when a constexpr calls an imported function, or on 'requires failed in call to'."
---

# Compile-time calls across modules, and failed contracts

```text
main.smith:18:17: error: constexpr variable 'bad' must be initialized by a constant expression
main.smith:10:15: note: requires failed in call to 'contract::requires(false)'
main.smith:18:23: note: in call to 'make_table(0)'
```

A `contract::requires`, `ensures`, `invariant`, `assert` or `test::check` inside a compile-time call stops the evaluator, and the error has the run-time report's form: the first note says what failed at the contract's line, then the stack of calls, innermost first, each with file, line and arguments (`smith-contract`). A bad argument is refused at compile time, with the stack to prove it.

## A function of another module runs too

Any exported function of an import, with the private functions, types and constants of its module that it uses and the exports of the modules that module imports, runs where C requires a constant: in `static_assert`, `constexpr` initializers, enum values, case labels, array bounds, an `if constexpr` condition and a generic's computed result type (`export shapes::slice_of(T, counts::dims(2, 3)) make(...)`).

- Nothing is written in the importer: the module's own compile wrote the function's lowered text to a `.body` file beside its header, and the importer defines what it mentions after its last `import` line, as copies that emit no code (the module's symbol stays the function). Imports of imports work.
- **Limits:** a function that reads or writes a variable of its module that is not `constexpr` is not carried (a copy would be a second variable; the call is the error of `smith-lang-constexpr`), and one whose evaluation calls a generic's instance is not carried yet.
- A call at run time asks for nothing.

Functions over `type` travel by another route (`smith-lang-type-values-more`). The basics: `smith-lang-constexpr`.
