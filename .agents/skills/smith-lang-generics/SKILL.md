---
name: smith-lang-generics
description: "Generic functions in .smith modules: `constexpr type T` parameters, `auto` parameters and `typeof(x)` (`check_equal(auto a, typeof(a) b)`), calls like `arena::push(&a, Point)` and `test::check_equal(a, b)`, `if constexpr`, instances. Load when a parameter list has `constexpr type T` or `auto x`, on `typeof(x)`, on `if constexpr`, on 'in instance ..., requested from here', 'is called without its type', 'converts to ..., the type of its first argument', 'cannot tell the type of', 'a type parameter is compile-time', or 'is a generic function: it can only be called'."
---

# Generic functions

This skill ships in the smith plugin, from `.agents/skills/smith-lang-generics/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; modules: `smith-lang-modules`. The long reference is [docs/smith-lang.md](../../../docs/smith-lang.md) ("Generic functions").

A function of a module with `constexpr type T` or `auto x` parameters is written once and compiled for each type it is called with. `constexpr` in a parameter list means compile-time: the parameter is in no generated function. A bare `type T` is an error (``a type parameter is compile-time: write `constexpr type T` ``).

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
i32 big = pick::larger(x, y);                              // `typeof(a) larger(auto a, typeof(a) b)`: the type is x's
```

## Calls

- **Every parameter is passed.** The type of a `constexpr type T` is the argument where the parameter stands (`list::make_two(arena, i32)`, `math::max(u32)`); leaving it out is an error (``push is called without its type `T`; pass the type: arena::push(&a, T)``). Where an argument carries the type, use `auto`.
- **`auto x`** is a parameter whose type is its argument's: `export void check_equal(auto a, typeof(a) b)`. One instance for each distinct canonical type (or list of types, with several `auto`s and `constexpr type`s, 4 at most). `auto` is `auto name` only: no `auto *p`, no `const auto x`. The type of the argument is a name, a field chain, `*e`, `&e`, `e[i]`, a call (a call to another generic has its result's type), a cast, `sizeof`, a literal, the lanes of an ext-vector, and the binary operators and `?:` of C over them, in the scope the parser is in (a local declared by an earlier statement counts); arrays are pointers and qualifiers are dropped. An argument it cannot type (`a++`, a compound literal, a name it does not know) is ``cannot tell the type of `a++`, the argument of `f`: ...; give it a type with a cast or a variable``. Inside a generic's body, an argument that is a parameter of that generic (`value_of(a)`) has the instance's type.
- **`typeof(x)`** names an `auto` parameter's type in later parameters, the body and the return type, written before the name as in C: `export typeof(x) sqrt(auto x)`. It is the instance's type, so `if constexpr (typeof(x) == f32)` works; `typeof` of anything else is plain C.
- **Conversions.** A later argument converts to `typeof(a)` by C's rules, except that a conversion that changes the value is an error at the call, with no warning flag needed: ``the argument `300` of `check_equal` converts to u8, the type of its first argument `small`, and changes its value; cast it, `(u8)300`, or the first argument, `(i32)small` ``. A literal and an expression of literals, `constexpr` integers (the file's, or a C header's through `c::`, of any signedness: `c::bpe_failed`, a `uint32_t` of all ones, is 4294967295 and fits a `u32`) and enumerators (`c::Style_Count - 1`) (`1 << 4`, `flag_a | flag_b`) are judged by value (`0.1` is not an `f32`, `0.5` and `0.1f` are; `-1` is not a `u32`); any other argument by type: its range must fit the target's (`u8` to `u32`, `i32` to `f64` yes; `u32` to `u8`, `i32` to `u32`, `i32` to `f32`, `f64` to `f32` no). Bools, enums and pointers convert by C's rules; an argument whose type cannot be said is not judged. Fix it with the cast the message names.
- Calls may nest (16 deep), and a generic may call generics: `list::make(a, T)` in a body uses the instance's `T`. A generic is not a value: ``is a generic function: it can only be called, with its type``.
- C and C++ cannot call a generic (the header has a `[[clang::unavailable]]` placeholder and a `// smith-generic:` line for importing modules); give C a typed wrapper with `[[smith::c_name]]`.

- **`if constexpr` reads a C type** (`T == c::V2`, `typeof(v) == c::V2`, a struct or a vector typedef of an imported header) and `target::` facts (`target::static_analyzer`) in a generic, as outside one.
- **Without a compiler.** `smith cc --smith-header` (the header step of `smith build`) writes declarations only and skips a call in a body whose type it cannot tell, so a module's body may call `test::check_equal(a, 1)`; `smith cc --smith-lower` (bootstrap/, the stage0) has no compiler to type an `auto` argument and reports the error above, so a bootstrapped module writes the types of the generics it calls out (`arena::push_array(arena, f64, count)`) and calls no `auto` function (it uses the builtins, `__builtin_isfinite(x)`).

## Instances

One function per distinct list of types, compared by the parser's canonical spelling (`u32` and `uint32_t` are one). It is a weak, hidden definition written at the end of the file (`always_inline` for an `inline` generic), named `pick_larger__i32`, with the symbol `_pick::larger(int)`, so copies from other files fold at link time. The debugger calls it `pick::larger<i32>` and shows the generic's own file and lines.

## `if constexpr`

```c
export i32 describe(auto value) {
    if constexpr (typeof(value) == f32) {
        return 1;
    } else if constexpr (is_integer(typeof(value)) && sizeof(typeof(value)) == 8) {
        return 2;
    } else {
        static_assert(false, "describe: not a number");     // a constraint is a static_assert in a final else
    }
}
```

In a generic's body, where it chooses the branch for each instance; in any other function body it chooses by the target (`if constexpr (target::arch == target::aarch64) { } else { }`, see `smith-lang`), with blocks for branches. In a generic the condition is a constant expression: integers, `true`, `false`, the operators of C, `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (`T` may be `typeof(x)`), and `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`. The branch not taken is never given to the parser, so it may use what `T` has not (`value.x`).

## Errors

Every error is at the generic's source, with a note at the call that asked for the instance: ``main.smith:13:20: note: in instance num::add(Point), requested from here``. A call that cannot be written, or a generic with an error in its head, is one error and nothing after it.

## Not built

Generic types, functions that return `type`, argument lists (`auto... rest`), compile-time value parameters (`constexpr u32 tile`), overloading, specialization, variadic lists of types, pointers to generics, a function-local type as `T`, and a `target::` fact in a generic's condition when lowering without the compiler (`smith cc --smith-lower`; a compile accepts it).
