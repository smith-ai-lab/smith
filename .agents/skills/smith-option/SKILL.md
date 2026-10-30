---
name: smith-option
description: "Using smith::option (no import: option(T)): a value that may be absent, `return v;` or `.none`, in place of a sentinel or an out parameter. Load when code writes `option(`, or on a question about returning 'no value' from a function in .smith."
---

# A value that may be absent: smith's option

This skill ships in the smith plugin, from `.agents/skills/smith-option/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`option` is in scope in every module with no import; `smith::option` (`lib/smith/option.smith`, readable source, its header what the compiler reads) defines the one type function, `option(type T)`: an enum with a payload (`smith-lang-enums`), `some` carrying a `T` and `none` carrying nothing. `option(usize)` written anywhere, in any module, is one type. A module cannot declare its own `option` (an error names the built-in).

```smith
module absent;

export typedef option(usize) Index; // name it at file scope once, then use Index

export Index find(const char *text, char wanted) {
    for (usize i = 0; text[i] != 0; i++) {
        if (text[i] == wanted) return i;       // a value converts to {.some = i}
    }
    return .none;
}

export usize or_else(Index at, usize otherwise) {
    if (at == .some) return at.some;       // `.some` is read only where `at == .some` holds
    return otherwise;
}
```

- **A value converts to `.some`** wherever an `option` is expected (an initializer, an assignment, `return`, an argument), with C's conversions for that context (`option(i64) x = 5;`); `{.some = v}` still works. Absence stays explicit: `.none`. It is an error, naming the explicit form, to convert to an `option` of an `option` (`option(option(i32))` given an `i32`) or `nullptr` to an `option` of a pointer (write `.none`). An `option` never becomes a value (use `try`, `switch` or `x == .some`), and `x == 5` is an error. In a generic's body write `{.some = v}`.
- A generic may write `option(T)` or `option(typeof(x))` in its head and body (each instance gets the struct of its own type).
- The argument may be a call of a function that returns `type`: `option(slice_of(T, 1)) view(constexpr type T, T *xs, usize n)` is an option over a slice. A caller names it through a typedef of the slice, `typedef T Row[:]; typedef option(Row) MaybeRow;`. A braceless `if (n == 0) return .none;` before `return {.some = v};` compiles clean.
- `option(usize)` may be named first anywhere a type is written, inside a function body too: its struct is defined there, in the block, and C23 makes it the same type as the file-scope one (as the very first mention, `= .none` is not read yet: write `{.some = v}` or mention it in an earlier statement).
- From C the type is the struct `smith_enum_option_<T>` (a tag, a union): unchecked there.
- To carry a reason for the absence use `smith-result`.
