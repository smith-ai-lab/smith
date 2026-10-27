---
name: smith-lang-rest
description: "The arguments of a generic and the loop over them: `auto... rest` (`rest...`, `rest[i]`, `countof(rest)`), `for constexpr (auto x : rest)`, `for constexpr (usize i = 0; i < countof(rest); i++)`, a type-checked loop by `typeof(rest[i])`, and why `auto args = {...}` (a built list) is gone. Load when a generic loops over its arguments, or on 'is the tuple of the arguments', 'built lists are gone', 'is out of range: `rest', 'in `for constexpr`', 'for constexpr is the loop of a generic'."
---

# `auto... rest` and `for constexpr`

C's `va_list` loses every type; here each argument keeps its own, and a loop over them runs while the instance is written, so nothing is left for run time.

```smith
module packs;

export f64 sum(auto... args) {                          // any number of arguments, each with its own type
    f64 total = 0;
    for constexpr (usize i = 0; i < countof(args); i++) {   // C's own `for`, run at compile time
        if constexpr (is_integer(typeof(args[i])) || is_float(typeof(args[i]))) {
            total += (f64)args[i];
        } else {
            static_assert(false, "sum: not a number");  // at the call that asked
        }
    }
    return total;
}

export f64 demo() {
    return sum(1, 2.5f, (i64)3);                        // 6.5
}
```

Smith is C plus compile time: each form is a C construct run while an instance is written, and lowers to plain C. They work in the body of a generic only (outside one, `for constexpr` is an error that says so).

## Rules

- **`auto... rest`** (the last parameter) is the tuple of the arguments that remain; the instance has one parameter for each, `rest_0`, `rest_1`, ... In the body: **`rest...`** spreads them wherever a list of expressions goes (a call's arguments or an initializer; an empty `rest` takes its comma); **`rest[i]`** is one, `i` a compile-time number (a literal, `countof`, a `constexpr`, the variable of a `for constexpr`); **`countof(rest)`** is how many, a constant (`static_assert`, an array length, `if constexpr`). One pattern per argument: `smith-lang-rest-expand`.
- **`for constexpr (usize i = 0; i < countof(rest); i++) body`**: the init, condition and step are constants (the variable, `countof`, numbers; step `i++`, `i--`, `i += n`, `i -= n`, `i = n`), and the body is written once per turn with `i` as its number. A loop may be nested, count down, run no turn, or take at most 1024. A bound that is no constant is an error naming it.
- **`for constexpr (auto x : rest)`** (and `for constexpr (usize i, auto x : rest)`) visits each argument: the body is written once per argument with `x` as `rest[n]`, so `x` has that argument's own type. The same form loops over `fields(T)` (`smith-lang-for`).
- **By type**: `if constexpr (is_integer(typeof(rest[i])))`, `typeof(rest[i]) == char *`, `sizeof(rest[i])` choose by each argument's own type (`smith-lang-if-constexpr`).
- `rest...`, `rest[k]` (alone or in an expression, `twice(rest[k] + 1)`) and locals can be forwarded to another generic; `rest` alone cannot. A variable named like an argument (`rest_0`) is an error: it would be that argument. The debugger shows `rest_1` at the generic's own lines.
- **Built lists are gone**: `auto args = {a, b};` and `args = {args..., x};` in a generic's body are an error that says so (outside a generic `auto x = {...}` is C's own error). Hold values in a struct, a tuple (`smith-lang-tuples`) or an array.

## Errors

- ``` `r` is the tuple of the arguments, and is not passed whole ```: spread it with `r...`, or write `r[i]` or `countof(r)`. Passing the tuple whole, or returning it, is not built; its struct is `typeof(rest)` and `a->[rest]...` its members (`smith-lang-generic-entry`).
- ``` `r[3]` is out of range: `r` has 2 arguments ```; a run-time index says which is not a constant.
- ``built lists are gone: write a struct or a tuple type ...``: `auto x = {r...};` in a generic.
- ``` `for constexpr` is the loop of a generic function's body ... a plain function's loop is `for` ```.

Generics: `smith-lang-generics`. The long reference: [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Argument lists", "`for constexpr`").
