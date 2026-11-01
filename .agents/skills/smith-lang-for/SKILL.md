---
name: smith-lang-for
description: "The range `for` of .smith: `for (T x : xs)` and `for (Usize i, T x : xs)` over a slice, a cut or a fixed array, `T *p` to write through, `auto` and concepts, and `for constexpr (auto f : fields(T))` over a compile-time list. Load when a loop visits every element of a slice or an array, or on 'a range `for` loops over a slice', 'the body of a range `for` is a block', 'the index of a range `for` is a `Usize`'."
---

# `for (T x : xs)`

A loop over a slice written by hand repeats its bounds, and a wrong `<=` or slice is an off-by-one. The range `for` cannot get them wrong and its elements need no check. The `:` at the top of a `for` header is invalid C, so no C file changes meaning.

```smith
module loops;

export I32 sum(const I32 xs[:]) {
    I32 total = 0;
    for (I32 x : xs) {                   // the common case: a copy of each element
        total += x;
    }
    return total;
}

export void scale(I32 xs[:], I32 factor) {
    for (Usize i, I32 *p : xs[1:]) {     // the index counts from 0 within what is looped; `i32 *p` points at the element
        *p *= factor + (I32)i;
    }
}
```

## Rules

- **Three loops, in this order**: `for (T x : xs)`; `for (Usize i, T x : xs)` for the position; C's `for (Usize i = 0; i < countof(xs); ++i)` for a step, a reverse walk, two slices or a changing bound.
- **Only slices and fixed arrays**: `T xs[:]`, a cut `xs[a:b]`, `T xs[N]` and an array parameter `T xs[N]`, of one dimension. Anything else is an error that names the C loop. There is no iterator, no `0..n`, no `_`, no third variable and no structured binding.
- **The declared type chooses**: `T x` copies each element (C's conversions apply); `T *p` points at the element, so writing through it changes the slice; with a slice of pointers `Node *n` copies each pointer and `Node **pn` points at each slot; `auto x` and a concept (`arithmetic x`, `smith-lang-concepts`) take the element's type; `const` is as in a declaration. `auto *p` is not C: write `T *p`, in a generic `typeof(xs[0]) *p`.
- **The index is a `Usize`** (any other type is an error that says so), counted from 0 within what is looped: over `xs[2:5]` it runs 0, 1, 2.
- **The body is a block**: `for (T x : xs) { ... }`. `break`, `continue`, `try`, `defer` (it runs at the end of each turn) and affine elements (`for (random::Key k : keys)`: each is used once) work as in C's loop.
- **Lowering**: a slice is evaluated once, before the loop, and read through its pointer with no bounds check (the index is in range by construction). A fixed array and the body of a generic are `for (Usize i = 0; i < countof(xs); i++)` with `T x = xs[i];`, so there `xs` is written as a name or a member (it is evaluated each turn).

## Over a compile-time list

In a generic's body `for constexpr` has the same range form, unrolled: each turn is a copy of the body with the name bound to that turn's element, so `x` can have another type each turn.

```smith
module show;

import <stdio.h>;

export void print(constexpr Type T, T *value) {
    for constexpr (auto f : fields(T)) {                    // every field of T (`smith-lang-reflection`)
        c::printf("%s ", f.name);
        if constexpr (is_float(typeof(T.[f])) || is_integer(typeof(T.[f]))) c::printf("= %g\n", (F64)value->[f]);
    }
}

export void each(auto... rest) {
    for constexpr (Usize i, auto x : rest) {                // each argument has its own type (`smith-lang-rest`)
        c::printf("%zu: %g\n", i, (F64)x);
    }
}
```

The list is `fields(T)` or a generic's `rest`; the index loop `for constexpr (Usize i = 0; ...)` stays valid. A constexpr array and `parameters(F)` are not built yet. Errors name the fix (C's loop, a block, a `Usize`).
