---
name: smith-lang-lambdas
description: "Lambdas in .smith that capture nothing: `[](i32 a) -> i32 { return a * 2; }`, `[](i32 a) { ... }` and `[] { ... }`, an expression that is a plain function and converts to a function pointer (qsort, a callback, a table). Load when a .smith file writes `[](` or `[] {`, passes a function pointer to C, or on 'belongs to the enclosing function', 'this `return` has another type than the lambda's' or 'smith_lambda_'."
---

# Lambdas that capture nothing

A lambda is an expression: `[](parameters) -> type { body }`. It captures nothing, so it is a plain C function and its value is the function's address. `[` starting an expression is a syntax error in C, so the form is smith's.

```smith
module lambdas;

import <stdlib.h>;

export i32 apply(i32 (*f)(i32), i32 x) {
    return f(x);
}

export i32 demo() {
    i32 doubled = apply([](i32 a) -> i32 { return a * 2; }, 4);      // an argument
    i32 (*triple)(i32) = [](i32 a) { return a * 3; };                // the type comes from the first `return`
    void (*nothing)() = [] { };                                  // no parameters: `[]` alone, then the body
    i32 values[3] = {3, 1, 2};
    c::qsort(values, 3, sizeof(i32), [](const void *a, const void *b) -> int {
        return *(const i32 *)a - *(const i32 *)b;                    // a C callback
    });
    nothing();
    return doubled + triple(1) + values[0];                          // 8 + 3 + 1
}
```

## Rules

- **Where**: in a function body (an argument, an initializer, an assignment, after `return`), and in a file-scope initializer (`static i32 (*const f)(i32) = [](i32 a) { return a; };`, or inside the braces of a table).
- **The return type** is `-> T`, else the type of the first `return` when it is the body's first statement, else `void` (no `return`). Write `-> T` for any other body. Every later `return` is checked against an inferred type: `this `return` has another type than the lambda's`; `-> T` says which.
- **No captures.** A name the body uses from the enclosing function is an error that names it: `` `factor` belongs to the enclosing function: ... a function pointer can't carry captures ``. Names of the module, the lambda's own parameters and locals, and `c::` names are fine.
- **No name**, so no recursion. Effects are inferred like any function's. Parameters are written types.
- **Underneath**: a function `smith_lambda_<file hash>_<place>` written after the enclosing function (hidden, so an external symbol that no other file sees); a file-scope lambda is a `static` function written before its declaration. A lambda inside a lambda is lifted the same way.
- A lambda in a `static` local's initializer is not constant: use file scope.

Overview: `smith-lang`. Reference: [knowledge/smith-lambdas.md](../../../knowledge/smith-lambdas.md).
