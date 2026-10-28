---
name: smith-lang-slice-qualifiers
description: "Qualifiers on a slice as C has them on a pointer: `const U8 xs[:]` (read-only elements), `U8 xs[const :]` (the slice itself fixed), `U8 xs[restrict :]` (no other pointer overlaps), `const U8 src[const restrict :]`, const slices in generics and in more dimensions. Load when a .smith file has `const`, `restrict` or `static` inside `[ :]`, or on 'discards qualifiers', 'read-only variable is not assignable', 'has no meaning for a slice' or 'a qualifier goes in the first bracket'."
---

# Qualifiers on a slice

C's rules for pointers, carried over unchanged. The element's qualifiers go with the element type; the slice's own go inside its brackets, after C99's `int a[const 10]` (`int *const a`) and `int a[restrict 10]`.

```smith
module quals;

export Usize sum(const U8 xs[:]) {                      // west const: the elements are read-only; `U8 const xs[:]` is the same type
    Usize total = 0;
    for (Usize i = 0; i < countof(xs); i++) total += xs[i];
    return total;
}

export void copy(U8 dst[restrict :], const U8 src[const restrict :]) {   // no other pointer reached by the call overlaps dst
    for (Usize i = 0; i < countof(dst) && i < countof(src); i++) dst[i] = src[i];
}

export Usize fixed(U8 xs[const :]) {                    // the slice (pointer and count) can't be reassigned or re-cut
    xs[0] = 1;                                          // the elements are writable
    return countof(xs);
}

export Usize demo() {
    U8 raw[4] = {1, 2, 3, 4};
    U8 out[4] = {};
    U8 plain[:] = raw;
    const U8 view[:] = plain;                           // U8[:] to const U8[:] is implicit; the reverse is an error
    copy(out, view);
    const char word[:] = "abc";                         // points into the literal, count 3, no copy
    return sum(view[1:]) + fixed(out) + countof(word);
}
```

`const F32 m[:][:] = grid;` takes the innermost element's qualifier; `F32 rows[const :][2]` the first bracket's.

## Rules

- **Conversions follow C's pointer rules.** `U8[:]` to `const U8[:]` is implicit (a call, `=`, an initializer, a return); the reverse discards qualifiers. A cut `xs[a:b]` of a `const U8[:]` keeps the `const`. `restrict` goes either way. A write through a const element and `r = ...` of a `[const :]` slice are clang's own errors.
- **Generics.** A `const U8[:]` argument passes as itself: `typeof(xs)`, `is_slice` and `fields(T)` keep the element's `const`, and `stats::count(const_slice)` is a different instance from `stats::count(plain_slice)`. The slice's own `const` and `restrict` are no part of the type a value holds (`typeof(dst)` of `U8 dst[restrict :]` is `U8[:]`). In a generic's body, a call argument, an initializer (`const U8 v[:] = xs;`) or an assignment (`v = ys;`) that is the bare name of a slice converts as in an ordinary function; a `return` and other expressions are not converted there.
- **`auto` takes the same forms:** `auto dst[restrict :]`, `const auto src[const :]` and `const auto xs[:]` in a generic. The instance takes the argument's slice as it is: `const` in the bracket makes the parameter `const`, `restrict` makes the instance's parameter the `restrict` slice (a plain argument converts, as in the non-generic form), so `smith prove` checks the overlap of the arguments at each call of the generic too. A second slice of the first one's element is `const typeof(dst[0]) src[const :]`: its type follows `dst`, a slice of another qualification converts (`U8[:]` to `const U8[:]`), and an array or a string literal becomes the slice (`slice::equal(name, "main")`).
- **Underneath:** `const T` makes `struct smith_array_const_u8_n` with a `const T *data`; `[const :]` makes the variable `const`; `[restrict :]` makes `data` `__restrict` in `struct smith_array_restrict_u8_n`, which a call converts to and from the plain one. A slice is two integers by value, so the IR has no `noalias`; `restrict` is in the debug info.
- `smith fmt` keeps the space of `[const :]` and `[const restrict :]`, and keeps `char const xs[:]` or `const char xs[:]` as written.

## Errors

- ``converting a slice of `const U8` to a slice of `U8` discards qualifiers``
- `[static :]` and `[volatile :]` have no meaning for a slice (`volatile` goes on the element: `volatile U8 xs[:]`).
- a qualifier in a later bracket (`F32 m[:][const :]`): only the outermost bracket carries qualifiers; the rows are views with no pointer of their own.
- `smith prove` refuses a call that passes overlapping slices to `restrict` ones (`smith-prove`).

Slices: `smith-lang-arrays`; a pointer to a slice: `smith-lang-slice-pointers`.
