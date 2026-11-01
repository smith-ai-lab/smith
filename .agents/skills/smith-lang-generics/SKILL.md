---
name: smith-lang-generics
description: "Generic functions in .smith modules: `constexpr type T` parameters (`arena::push(&a, Point)`), `constexpr usize i` (a number known at compile time), `auto` parameters and `typeof(x)` (`check_equal(auto a, typeof(a) b)`), instances, the errors at a call. Load when a parameter list has `constexpr type T` or `auto x`, on `typeof(x)`, or on 'in instance ..., requested from here', 'is called without its type', 'a type parameter is compile-time', 'is a generic function: it can only be called'."
---

# Generic functions

A function of a module with `constexpr type T` or `auto x` parameters is written once and compiled for each type it is called with. C writes a macro or one copy per type; here the compiler writes the copies.

```smith
module pick;

import std::arena;

export typeof(a) larger(auto a, typeof(a) b) {          // one instance per type: C needs a macro or a copy for each
    return a > b ? a : b;
}

export T *fresh(arena::Arena *a, constexpr type T) {    // T is a type argument: usable in the signature and the body
    T *p = arena::push(a, T);                            // zeroed
    return p;
}
```

Called as `pick::larger(3, 5)` (an `i32`), `pick::larger(2.5f, 1.0f)` (an `f32`) and `Point *p = pick::fresh(&a, Point);`.

## Rules

- **`constexpr`** in a parameter list means compile-time: the parameter is in no generated function. **Every parameter is passed**: the type of `constexpr type T` is the argument where the parameter stands (`list::make_two(arena, i32)`, `math::max(u32)`). Where an argument carries the type, use `auto`.
- **`auto x`** is a parameter whose type is its argument's; one instance for each distinct canonical type (`u32` and `uint32_t` are one), at most 32 parameters. `auto` is also the element of a parameter's declarator: `const auto xs[:]` and `auto m[:][:]` (a slice, one or two dimensions), `const auto *p` (a pointer) and `auto xs[4]` (a fixed array) keep their shape and qualifiers, each use is its own instance, and `typeof(xs[0])` or `typeof(*p)` names the element in the rest of the signature (`usize find(const auto xs[:], typeof(xs[0]) v)`); qualifiers in the bracket (`auto dst[restrict :]`), a pointer to a slice (`const auto (*xs)[:]`), a second slice of the first one's element (`const typeof(a[0]) b[:]`, which takes an array or a string literal) and a slice named by a typedef all work (`smith-lang-slice-qualifiers`, `smith-lang-slice-pointers`). A concept takes the same places (`const arithmetic xs[:]`, `integer *p`: `smith-lang-concepts`). Not yet: `const auto x`, `auto` in a return type, member, typedef, cast or a local without an initializer (write `typeof(x)`). `const auto xs[:]` takes a slice of any elements and reads them, as `const T *` takes a `T *`: writing through it is an error. How the type is read and what converts: `smith-lang-generics-auto`.
- **A generic may return a slice of its type parameter**, written bare, `export T[:] window(constexpr type T, T *p, usize a, usize b)` (or `typeof(T[:])`): one result type per instance, and `p[a:b]` in the body slices the pointer `T *p` (both bounds, `a <= b` checked); other pointer shapes (`const T *p`, an expression other than the parameter) are not built.
- **`typeof(x)`** names an `auto` parameter's type in later parameters, the body and the return type, written before the name as in C (`export typeof(x) sqrt(auto x)`; `typeof(*x)` is the pointee). It is the instance's type, so `if constexpr (typeof(x) == f32)` works (`smith-lang-if-constexpr`). `typeof` of anything else is plain C.
- **`constexpr usize i`** (any integer type) takes a number the compiler knows (a literal, a `constexpr`, a `for constexpr` variable; else an error at the call naming the argument): one instance for each value, and `i` in the body is that number, so `constexpr Field f = fields(T)[i];` works.
- A bare `type T` that the head uses as a type is an error; a bare `type t` that is only a value is a type value (`smith-lang-type-values`).
- **Instances** are weak, hidden definitions at the end of the file (`always_inline` for an `inline` generic), named `pick_larger__i32` with the symbol `_pick::larger(int)`, so copies from other files fold at link time. The debugger calls it `pick::larger<i32>` at the generic's own file and lines.
- `lambda(F) f` (code to run, one instance per lambda: `smith-lang-lambda-param`), `constexpr auto f` (a function as an argument), a computed result type, nesting, private names, C and no compiler: `smith-lang-generics-more`. `auto... rest`: `smith-lang-rest`.

## Errors

Every error is at the generic's source, with a note at the call that asked: ``main.smith:13:20: note: in instance num::add(Point), requested from here``.

- ``push is called without its type `T`; pass the type: arena::push(&a, T)``
- ``a type parameter is compile-time: write `constexpr type T` ``
- ``` `larger` is a generic function: it can only be called, with its type ```: a generic is not a value (no pointer to one); its name is an argument only where `constexpr auto f` expects it (`smith-lang-generics-more`).
- a call that cannot be written, or a generic with an error in its head, is one error and nothing after it.

## Not built

Generic types, building a struct from a function that returns `type`, overloading, specialization, pointers to generics, a function-local type as `T`, and a `target::` fact in a condition under `smith cc --smith-lower` (a compile accepts it). The long reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Generic functions").
