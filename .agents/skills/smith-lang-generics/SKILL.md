---
name: smith-lang-generics
description: "Generic functions in .smith modules: `constexpr type T` parameters, calls like `arena::push(&a, Point)` and `test::check_equal(a, b)` with T inferred, `if constexpr`, instances. Load when a parameter list has `constexpr type T`, on `if constexpr`, on 'in instance ..., requested from here', 'conflicting types for', 'cannot tell ... from', 'a type parameter is compile-time', or 'is a generic function: it can only be called'."
---

# Generic functions

This skill ships in the smith plugin, from `.agents/skills/smith-lang-generics/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; modules: `smith-lang-modules`. The long reference is [docs/smith-lang.md](../../../docs/smith-lang.md) ("Generic functions").

A function of a module with `constexpr type T` parameters is written once and compiled for each type it is called with. `constexpr` in a parameter list means compile-time: the parameter is in no generated function. A bare `type T` is an error (``a type parameter is compile-time: write `constexpr type T` ``).

```smith fragment
module arena;
export T *push(Arena *a, constexpr type T) {              // T is usable in the signature and the body
    usize aligned = (a->used + alignof(T) - 1) & ~(alignof(T) - 1);
    T *result = (T *)(a->base + aligned);
    a->used = aligned + sizeof(T);
    return result;
}

// elsewhere
Point *p = arena::push(&a, Point);                         // the type is the argument in T's place
i32 big = pick::larger(x, y);                              // or left out: T is the type of x and y
```

## Calls

- The type is the argument where the parameter stands (`list::make_two(arena, i32)`), or every type argument is left out and inferred: `pick::larger(a, b)` for `T larger(constexpr type T, T a, T b)`.
- **Inference** takes `T` from the parameters of the form `T x`, `const T *x`, `T *x` and `T x[]` (each `*` and `[]` dereferences the argument's type once). It types a name, a field chain, `*e`, `&e`, `e[i]`, a call, a cast and a literal, in the scope the parser is in: a local declared by an earlier statement counts. A literal adapts to the arguments that are not one. Arithmetic and conditionals are not typed: write the type. Inside a generic's own body the instance's locals are not yet known, so write the type there too. Errors: ``conflicting types for `T`: i32 and f64``, ``cannot tell `T` from `a + 1`: ...; write the type as the argument``, ``cannot tell `T` from the arguments of `alloc`: no parameter has the form `T x` ``.
- Calls may nest (16 deep), and a generic may call generics: `list::make(a, T)` in a body uses the instance's `T`. A generic is not a value: ``is a generic function: it can only be called, with its type``.
- C and C++ cannot call a generic (the header has a `[[clang::unavailable]]` placeholder and a `// smith-generic:` line for importing modules); give C a typed wrapper with `[[smith::c_name]]`.

## Instances

One function per distinct list of types, compared by the parser's canonical spelling (`u32` and `uint32_t` are one). It is a weak, hidden definition written at the end of the file (`always_inline` for an `inline` generic), named `pick_larger__i32`, with the symbol `_pick::larger(int)`, so copies from other files fold at link time. The debugger calls it `pick::larger<i32>` and shows the generic's own file and lines.

## `if constexpr`

```c
export i32 describe(constexpr type T, T value) {
    if constexpr (T == f32) {
        return 1;
    } else if constexpr (is_integer(T) && sizeof(T) == 8) {
        return 2;
    } else {
        static_assert(false, "describe: not a number");     // a constraint is a static_assert in a final else
    }
}
```

Only in a generic's body. The condition is a constant expression: integers, `true`, `false`, the operators of C, `sizeof(T)`, `alignof(T)`, `T == U` and `T != U`, and `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`. The branch not taken is never given to the parser, so it may use what `T` has not (`value.x`).

## Errors

Every error is at the generic's source, with a note at the call that asked for the instance: ``main.smith:13:20: note: in instance num::add(Point), requested from here``. A call that cannot be written, or a generic with an error in its head, is one error and nothing after it.

## Not built

Generic types, functions that return `type`, compile-time value parameters (`constexpr u32 tile`), overloading, specialization, variadic lists of types, pointers to generics, and a function-local type as `T`.
