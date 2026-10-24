---
name: smith-lang-rest
description: "The arguments of a generic and the loop over them: `auto... rest` (`rest...`, `pattern...`, `rest[i]`, `countof(rest)`), `for constexpr (usize i = 0; i < countof(rest); i++)`, a type-checked print by `typeof(rest[i])`, and why `auto args = {...}` (a built list) is gone. Load when a generic loops over its arguments, or on 'is the tuple of the arguments', 'built lists are gone', 'is out of range: `rest', 'in `for constexpr`', 'is the name of one of the arguments', '`...` expands nothing', 'an expansion does not nest', 'for constexpr is the loop of a generic'."
---

# `auto... rest` and `for constexpr`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-rest/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Generic functions: `smith-lang-generics`; tuples: `smith-lang-tuples`. The long reference is [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Argument lists" and "`for constexpr`").

Smith is C plus compile time: each form is a C construct run while an instance is written, and lowers to plain C. They work in the body of a generic only (outside one, `for constexpr` is an error that says so).

- **`auto... rest`** (the last parameter) is the tuple of the arguments that remain, each with its own type; the instance has one parameter for each, `rest_0`, `rest_1`, ... The body uses it in three ways. **`rest...`** spreads the arguments wherever a list of expressions goes: a call's arguments or an initializer (an empty `rest` takes its comma). **`rest[i]`** is the argument, `i` a compile-time number (a literal, `countof`, a `constexpr`, the variable of a `for constexpr`). **`countof(rest)`** is how many; it is a constant (`static_assert`, an array length, `if constexpr`).
- **`pattern...` expands a pattern**, an expression that names `rest`, to one copy per element, `rest` replaced by the element: `f(leaf(rest, off)...)` is `f(leaf(rest_0, off), leaf(rest_1, off), ...)`; `rest...` is the case where the pattern is `rest` alone. It stands as an argument of a call or of a braced initializer, among other arguments (`f(a, g(rest)..., b)`, `{g(rest)..., 0}`); the pattern may name `rest` several times, hold calls to generics and `x->[f]` (`rest->[f]...`), and `rest[i]` and `countof(rest)` in it are not the pack. An empty `rest` writes nothing and the comma before it (else the one after) goes with it: `f(a, g(rest)...)` is `f(a)`. Errors: a pattern that names no pack (`...` expands nothing: name an `auto...` parameter), and a `...` inside a pattern (expansions do not nest). `...` after an expression is invalid C, so the C rule allows it.
- **`rest` alone is an error** that says to spread it with `rest...` (`g(rest)`), index it or count it: passing the tuple whole, or returning it, is not built yet. A run-time index or an index out of range is an error that says which.
- **`for constexpr (usize i = 0; i < countof(rest); i++) body`** is C's own `for`, run at compile time: the init, condition and step are constants (the variable, `countof`, numbers; step `i++`, `i--`, `i += n`, `i -= n`, `i = n`), and the body is written once per turn with `i` as its number. A body that is one statement is the same; a loop may be nested, count down, run no turn, or take at most 1024. A bound that is no constant is an error naming it. There is no range-based `for`.
- **Types of the arguments:** `if constexpr (is_integer(typeof(rest[i])))`, `typeof(rest[i]) == char *`, `sizeof(rest[i])` choose by each argument's own type.
- A variable named like an argument (`rest_0`) is an error: it would be that argument.
- **Built lists are gone.** `auto args = {a, b};` and `args = {args..., x};` in a generic's body are an error that says so (outside a generic `auto x = {...}` is C's own error). Hold values in a struct or a tuple type (`smith-lang-tuples`), or an array.

```smith fragment
export void print(auto... args) {                 // a type-checked print: each argument by its own type
    for constexpr (usize i = 0; i < countof(args); i++) {
        if constexpr (is_integer(typeof(args[i])) && is_signed(typeof(args[i]))) {
            c::printf("%lld ", (long long)args[i]);
        } else if constexpr (is_float(typeof(args[i]))) {
            c::printf("%g ", (double)args[i]);
        } else {
            static_assert(false, "print: no format for this type");   // at the call that asked
        }
    }
}
```

The debugger shows `rest_1` at the generic's own lines. `rest...`, `rest[k]` (alone or in an expression, `twice(rest[k] + 1)`) and locals can be forwarded to another generic; `rest` alone cannot.
