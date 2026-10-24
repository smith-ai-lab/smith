---
name: smith-lang-generics
description: "Generic functions in .smith modules: `constexpr type T` parameters, `auto` parameters and `typeof(x)` (`check_equal(auto a, typeof(a) b)`), `constexpr auto f` (a function known at compile time), `auto... rest` (argument lists; their use is `smith-lang-rest`), calls like `arena::push(&a, Point)` and `test::check_equal(a, b)`, `if constexpr`, instances. Load when a parameter list has `constexpr type T`, `auto x`, `constexpr auto f` or `auto... rest`, on `typeof(x)`, on `if constexpr`, on 'in instance ..., requested from here', 'is called without its type', 'converts to ..., the type of its first argument', 'cannot tell the type of', 'a type parameter is compile-time', 'is not a function: the parameter', or 'is a generic function: it can only be called'."
---

# Generic functions

This skill ships in the smith plugin, from `.agents/skills/smith-lang-generics/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; modules: `smith-lang-modules`; `auto... rest` and `for constexpr`: `smith-lang-rest`. The long reference is [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Generic functions").

A function of a module with `constexpr type T` or `auto x` parameters is written once and compiled for each type it is called with. `constexpr` in a parameter list means compile-time: the parameter is in no generated function. A bare `type T` that the head uses as a type is an error (``a type parameter is compile-time: write `constexpr type T` ``); a bare `type t` that is only a value is a type value (`smith-lang-type-values`), and `T` can be passed to an ordinary function over `type` in a condition: `if constexpr (small(T))`.

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
- **`auto x`** is a parameter whose type is its argument's: `export void check_equal(auto a, typeof(a) b)`. One instance for each distinct canonical type (or list of types, with several `auto`s and `constexpr type`s, 32 parameters at most). `auto` is `auto name` only: no `auto *p`, no `const auto x`. The type of the argument is the compiler's: the lowering has the parser read `__typeof__(<the argument>)` in the scope it is in, so any expression has its exact type (`((u8 *)block)[n - 1]`, a member of an anonymous union, `a++`, a statement expression, a call to another generic, a constant of its own type), a local declared by an earlier statement counts, and nothing is evaluated; arrays are pointers and qualifiers are dropped. An argument the compiler does not accept (a name it does not know) is ``cannot tell the type of `x`, the argument of `f`: <the compiler's message>; give it a type with a cast or a variable``. A later argument the compiler folds to a number is judged by its value and its own type. Inside a generic's body, an argument that is a parameter of that generic (`value_of(a)`), a local declared earlier as a type and a name (`i32 n = 3; twice(n)`), a parameter such as `typeof(proto) *p` (`twice(*p)`) or `rest[k]` in an expression (`twice(rest[k] + 1)`) has the instance's type; a local declared `auto` does not (give it a type).
- **`constexpr auto f`** takes a function known at compile time: pass its name (`loss`, `nn::loss`, `c::abs`), not a pointer; each function is its own instance, so `f(x)` is a direct call and `(void *)f` its address: `export typeof(x) twice(constexpr auto f, auto x) { return f(f(x)); }`, called `apply::twice(inc, 5)`. A pointer variable, `&inc`, `(inc)` or a number is an error naming the parameter. `f` taking the arguments is checked where the body calls it, at the instance. The debugger shows `apply::twice<apply::triple, int>`.
- **`auto... rest`** (the last parameter) takes any number of arguments, each with its own type, one instance for each list of types: `export typeof(x) call(constexpr auto f, auto x, auto... rest) { return f(x, rest...); }`. `rest...`, `rest[i]`, `countof(rest)` and `for constexpr` are `smith-lang-rest`.
- **`typeof(x)`** names an `auto` parameter's type in later parameters, the body and the return type, written before the name as in C: `export typeof(x) sqrt(auto x)`. Any `typeof(...)` of the parameter in the return type works (`typeof(*x)`: the pointee). It is the instance's type, so `if constexpr (typeof(x) == f32)` works; `typeof` of anything else is plain C.
- **A computed result type.** The result type may be a call that yields a `type`, run in the compiler for each instance: `export slice_of(T, countof(rest)) f(constexpr type T, auto... rest)` with `export type slice_of(type element, usize count)` an ordinary function over `type` (`smith-lang-type-values`): the result of `arena::push_slice(&a, f32, n, m)` is `f32[:][:]`. The arguments are the `constexpr type` parameters and `countof(rest)`; the header keeps the written call. A call that is not a type, or cannot be computed, is an error at the call naming the result type. The function is the file's own or an exported function over `type` of an import (the importer defines it, issue #107). In the body, `struct smith_array_T_n` names the result's struct, whatever its dimensions.
- **Conversions.** A later argument converts to `typeof(a)` by C's rules, except that a conversion that changes the value is an error at the call, with no warning flag needed: ``the argument `300` of `check_equal` converts to u8, the type of its first argument `small`, and changes its value; cast it, `(u8)300`, or the first argument, `(i32)small` ``. A literal and an expression of literals, `constexpr` integers (the file's, or a C header's through `c::`, of any signedness: `c::bpe_failed`, a `uint32_t` of all ones, is 4294967295 and fits a `u32`) and enumerators (`c::Style_Count - 1`) (`1 << 4`, `flag_a | flag_b`) are judged by value (`0.1` is not an `f32`, `0.5` and `0.1f` are; `-1` is not a `u32`); any other argument by type: its range must fit the target's (`u8` to `u32`, `i32` to `f64` yes; `u32` to `u8`, `i32` to `u32`, `i32` to `f32`, `f64` to `f32` no). Bools, enums and pointers convert by C's rules; an argument whose type cannot be said is not judged. Fix it with the cast the message names.
- Calls may nest (16 deep, a call may be the argument of a call in a body too: `twice(f, twice(f, x))`), and a generic may call generics: `list::make(a, T)` in a body uses the instance's `T`. **An exported generic's body may name the module's private functions, variables, types, constants and generics**: the header gets a section only importers read, and an importer still cannot write them (``is private to its module ... export it``). An exported declaration (a struct field's array bound) cannot name a private constant. A generic is not a value: ``is a generic function: it can only be called, with its type``.
- C and C++ cannot call a generic (a `// smith-generic:` line serves importing modules); give C a typed wrapper with `[[smith::c_name]]`.
- **`if constexpr` reads a C type** (`T == c::V2`, `typeof(v) == c::V2`, a struct or a vector typedef of an imported header) and `target::` facts (`target::static_analyzer`) in a generic, as outside one.
- **Without a compiler.** `smith cc --smith-header` (the header step of `smith build`) writes declarations only and skips a call in a body whose type it cannot tell, so a module's body may call `test::check_equal(a, 1)`; `smith cc --smith-lower` has no compiler to type an `auto` argument and reports the error above, so a module that is lowered that way writes the types of the generics it calls out (`arena::push(arena, f64)`) and calls no `auto` function (it uses the builtins, `__builtin_isfinite(x)`).

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

In a generic's body, where it chooses the branch for each instance; in any other function body it chooses by the target (`if constexpr (target::arch == target::aarch64) { } else { }`, see `smith-lang`), with blocks for branches. In a generic the condition is a constant expression: integers, `true`, `false`, the operators of C, `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (`T` may be `typeof(x)`), and `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`, `is_slice` (`T[:]`, a slice: `smith-lang-arrays`). The branch not taken is never given to the parser, so it may use what `T` has not (`value.x`). A call or a field in the condition (`table_size(sizeof(T)) > 64`, a `for constexpr` bound `i < config.count`) is evaluated by the compiler, so it must be an integer constant; the function must be defined in the file the instance is written in, or be an exported function of an import, or a private one of the generic's own module that it uses (`smith-lang-constexpr`, `smith-lang-type-values`): the importer defines what it mentions.

## Errors

Every error is at the generic's source, with a note at the call that asked for the instance: ``main.smith:13:20: note: in instance num::add(Point), requested from here``. A call that cannot be written, or a generic with an error in its head, is one error and nothing after it. A module's own exports may be written qualified in its generics (`tools::dup` in `smith::tools`), as an importer writes them.

## Not built

Generic types, building a struct from a function that returns `type`, compile-time value parameters (`constexpr u32 tile`), overloading, specialization, pointers to generics, a function-local type as `T`, and a `target::` fact in a generic's condition when lowering without the compiler (`smith cc --smith-lower`; a compile accepts it).
