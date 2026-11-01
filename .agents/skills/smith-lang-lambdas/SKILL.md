---
name: smith-lang-lambdas
description: "Lambdas in .smith that capture nothing: `[](I32 a) -> I32 { return a * 2; }`, `[](I32 a) { ... }` and `[] { ... }`, an expression that is a plain function and converts to a function pointer (qsort, a callback, a table). Load when a .smith file writes `[](` or `[] {` (also in a generic's body), passes a function pointer to C, or on 'belongs to the enclosing function', 'this `return` has another type than the lambda's' or 'smith_lambda_'."
---

# Lambdas that capture nothing

A lambda is an expression: `[](parameters) -> type { body }`. It captures nothing, so it is a plain C function and its value is the function's address. `[` starting an expression is a syntax error in C, so the form is smith's.

```smith
module lambdas;

import <stdlib.h>;

export I32 apply(I32 (*f)(I32), I32 x) {
    return f(x);
}

export I32 demo() {
    I32 doubled = apply([](I32 a) -> I32 { return a * 2; }, 4);      // an argument
    I32 (*triple)(I32) = [](I32 a) { return a * 3; };                // the type comes from the first `return`
    void (*nothing)() = [] { };                                  // no parameters: `[]` alone, then the body
    I32 values[2] = {3, 1};
    c::qsort(values, 2, sizeof(I32), [](const void *a, const void *b) -> int {
        return *(const I32 *)a - *(const I32 *)b;                    // a C callback
    });
    nothing();
    return doubled + triple(1) + values[0];                          // 8 + 3 + 1
}
```

## Rules

- **Where**: in a function body (an argument, an initializer, an assignment, after `return`), and in a file-scope initializer (`static I32 (*const f)(I32) = [](I32 a) { return a; };`, or inside the braces of a table).
- **The return type** is `-> T`, else the type of the first `return` when it is the body's first statement, else `void` (no `return`). Write `-> T` for any other body. Every later `return` is checked against an inferred type: `this `return` has another type than the lambda's`; `-> T` says which.
- **No captures.** A name the body uses from the enclosing function is an error that names it: `` `factor` belongs to the enclosing function: ... a function pointer can't carry captures ``. Names of the module, the lambda's own parameters and locals, and `c::` names are fine. A capture list `[x, y]` (locals' names, in a module) is read, but captures are not lowered yet (#958): the error names the first capture.
- **No name**, so no recursion; effects are inferred like any function's; parameters are written types. **Underneath**: a hidden function `smith_lambda_<file hash>_<place>` written after the enclosing function (a file-scope lambda: a `static` one before its declaration). A lambda in a `static` local's initializer is not constant: use file scope.

## In a generic

A lambda in a generic's body is one function for each instance, and its address knows the instance's types: the generic's `constexpr` parameters (`T`, `handler`) are not captures.

```smith
module later;

export typedef struct { void (*entry)(void *); alignas(16) U8 data[64]; } Slot;

export void defer(Slot *slot, constexpr auto f, auto... rest) {
    typeof(rest) args = {rest...};                  // the struct of the arguments
    *(typeof(rest) *)slot->data = args;
    slot->entry = [](void *raw) {                   // a function of this instance
        typeof(rest) *a = raw;
        f(a->[rest]...);                            // f(a->_0, a->_1, ...)
    };
}
```

- A run-time parameter of the generic used in the lambda is the captures error. No `-> T`: the type is the first `return`'s. A lambda in such a lambda is an error; at most 8 in a generic.

Overview: `smith-lang`; as a parameter, `Lambda(F)`: `smith-lang-lambda-param`. Reference: [knowledge/smith-lambdas.md](../../../knowledge/smith-lambdas.md).
