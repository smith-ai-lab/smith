---
name: smith-lang-generics-more
description: "The rest of generics in .smith: `constexpr auto f` (a function passed by name, a direct call), a computed result type (`slice_of(T, countof(rest)) f(...)`), nested calls, private names in an exported generic's body, C and C++ cannot call a generic, lowering without a compiler. Load on 'is not a function: the parameter', a generic that takes a function, or a generic whose result type is computed."
---

# Functions as arguments, computed results, and the limits

```smith
module apply;

export typeof(x) twice(constexpr auto f, auto x) {   // f is known at compile time: f(x) is a direct call
    return f(f(x));
}

export i32 inc(i32 x) {
    return x + 1;
}

export i32 seven() {
    return twice(inc, 5);                            // pass the name: not a pointer, not &inc
}
```

## Rules

- **`constexpr auto f`** takes a function by name (`loss`, `nn::loss`, `c::abs`); each function is its own instance, so `f(x)` is a direct call and `(void *)f` its address. A pointer variable, `&inc`, `(inc)` or a number is an error naming the parameter (``is not a function: the parameter ...``). That `f` takes the arguments is checked where the body calls it, at the instance. The debugger shows `apply::twice<apply::triple, int>`.
- **A generic function as `f`** (`bool ascending(arithmetic a, typeof(a) b)`, `sort_by(xs, ascending)`, `sort_by(xs, order::descending)`) is instantiated at the use: each call `f(a, b)` in the instance makes the instance its argument types ask for, as a direct call would, so one name serves every element type. It must be the whole argument, and it can be forwarded: inside the instance, `f` as the whole argument of a call to another `constexpr auto` parameter passes the generic on. It only exists as `f(...)`: `(void *)f`, `&f` and passing it to an `auto` parameter are errors (``is a generic function: ...``). A generic that cannot take the arguments the body gives is the error of a direct call, at the instance.
- **A computed result type**: the result type may be a call that yields a `type`, run in the compiler for each instance: `export slice_of(T, countof(rest)) f(constexpr type T, auto... rest)` with `export type slice_of(type element, usize count)` an ordinary function over `type` (`smith-lang-type-values`); `arena::push_slice(&a, f32, n, m)` is `f32[:][:]` this way. The arguments are the `constexpr type` parameters and `countof(rest)`; the header keeps the written call. A call that is not a type, or cannot be computed, is an error at the call naming the result type. The function is the file's own or an exported one of an import (the importer defines it). In the body, `struct smith_array_T_n` names the result's struct, whatever its dimensions.
- **Nesting**: calls nest (16 deep; a call may be an argument in a body: `twice(f, twice(f, x))`), and a generic may call generics: `list::make(a, T)` in a body uses the instance's `T`.
- **Private names**: an exported generic's body may name the module's private functions, variables, types, constants and generics: the header gets a section only importers read, and an importer still cannot write them (``is private to its module ... export it``). An exported declaration (a struct field's array bound) cannot name a private constant. A module's own exports may be written qualified in its generics (`tools::dup` in `smith::tools`).
- **`if constexpr`** reads a C type (`T == c::V2`, a struct or vector typedef of an imported header) and `target::` facts (`target::static_analyzer`) in a generic, as outside one (`smith-lang-if-constexpr`).
- **C and C++ cannot call a generic** (a `// smith-generic:` line serves importing modules): give C a typed wrapper with `[[smith::c_name]]` (`smith-lang-exports`).
- **Without a compiler**: `smith cc --smith-header` writes declarations only and skips a call in a body whose type it cannot tell, so a module's body may call `test::check_equal(a, 1)`. `smith cc --smith-lower` has no compiler to type an `auto` argument and reports the error of `smith-lang-generics-auto`: a module lowered that way writes the types of the generics it calls (`arena::push(arena, f64)`) and calls no `auto` function (it uses the builtins, `__builtin_isfinite(x)`).

Generics: `smith-lang-generics`; `auto... rest`: `smith-lang-rest`.
