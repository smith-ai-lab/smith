---
name: smith-lang-lists
description: "Compile-time lists and loops in a generic's body: `auto... rest` (`rest...`, `rest[i]`, `countof(rest)`), `auto args = {...}` lists, assigning a list (`args = {args..., x}`), `for constexpr (usize i = 0; i < countof(rest); i++)`, a type-checked print by `typeof(rest[i])`. Load when a generic loops over its arguments, builds a list, or on 'is a list: write', 'is a list of arguments', 'is out of range', 'in `for constexpr`', 'is the name of an element of the list', 'a list is written', 'a list's shape is known while the body is written', 'for constexpr is the loop of a generic'."
---

# Lists and `for constexpr`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-lists/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Generic functions: `smith-lang-generics`. The long reference is [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Lists and for constexpr").

Smith is C plus compile time: each form is a C construct run while an instance is written, and lowers to plain C. They work in the body of a generic only (outside one, `for constexpr` is an error that says so, and `auto x = {...}` is C's own error).

- **A list** is a value whose shape (how many elements, of which types) is known at compile time; its elements are ordinary run-time values. The argument list `auto... rest` is one: its elements are the parameters `rest_0`, `rest_1`, ... `auto args = {1, x, &dx};` makes another, each element a variable `args_0_0`, `args_0_1`, ... of its own type (C23's `auto`), evaluated once, where the statement stands. `{}` is the empty list. Elsewhere braces keep C's meaning.
- **`list...`** expands the elements wherever a list of expressions goes: a call's arguments (an empty list takes its comma), or another list: `auto both = {a..., b..., 7};`. **`list[i]`** is the element, `i` a compile-time number (a literal, `countof`, a `constexpr`, the variable of a `for constexpr`). **`countof(list)`** is how many; it is a constant (`static_assert`, an array length, `if constexpr`). A bare list, a run-time index or an index out of range is an error that says which.
- **Assigning a list makes a new binding:** `args = {args..., rest[i]};` declares `args_1_...` from `args_0_...` and the new elements; no variable changes type. It is a statement of its own, at the block where the list is declared (a `for constexpr` body counts as that place; its braces, and those of an `if constexpr` branch that assigns, are not written): in a run-time `if`, loop or block it is an error, since the list's shape would change at run time.
- **`for constexpr (usize i = 0; i < countof(rest); i++) body`** is C's own `for`, run at compile time: the init, condition and step are constants (the variable, `countof`, numbers; step `i++`, `i--`, `i += n`, `i -= n`, `i = n`), and the body is written once per turn with `i` as its number. A body that is one statement is the same; a loop may be nested, count down, run no turn, or take at most 1024. A bound that is no constant is an error naming it. There is no range-based `for`.
- **Types of the elements:** `if constexpr (is_integer(typeof(rest[i])))`, `typeof(rest[i]) == char *`, `sizeof(rest[i])` choose by each argument's own type (a list made with braces has its types only in the C written: `typeof(args[i])` in an `if constexpr` condition is an error).
- A variable named like an element (`rest_0`, `args_1_0`) is an error: it would be that element.

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

The debugger shows `rest_1` and `args_2_0` at the generic's own lines. A list passed to another generic (`other::f(args...)`) is typed by the compiler, so only the argument list `rest...` can be forwarded today.
