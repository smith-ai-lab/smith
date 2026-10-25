---
name: smith-option
description: "Using smith::option (no import: option(T)): a value that may be absent, `{.some = v}` or `.none`, in place of a sentinel or an out parameter. Load when code writes `option(`, or on a question about returning 'no value' from a function in .smith."
---

# A value that may be absent: smith's option

This skill ships in the smith plugin, from `.agents/skills/smith-option/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`option` is in scope in every module with no import; `smith::option` (`lib/smith/option.smith`, readable source, its header what the compiler reads) defines the one type function, `option(type T)`: an enum with a payload (`smith-lang-enums`), `some` carrying a `T` and `none` carrying nothing. `option(usize)` written anywhere, in any module, is one type. A module cannot declare its own `option` (an error names the built-in).

```smith
typedef option(usize) Index;        // name it at file scope once, then use Index

Index find(const char *text, char wanted) {
    for (usize i = 0; text[i] != 0; i++)
        if (text[i] == wanted) return {.some = i};
    return .none;
}

usize or_else(Index at, usize otherwise) {
    if (at == .some) return at.some;       // `.some` is read only where `at == .some` holds
    return otherwise;
}
```

- A generic may write `option(T)` or `option(typeof(x))` in its head and body (each instance gets the struct of its own type).
- The struct must be named at file scope before a function body uses it (`typedef option(usize) Index;`), as for any type function (`smith-lang-enums`).
- From C the type is the struct `smith_enum_option_<T>` (a tag, a union): unchecked there.
- To carry a reason for the absence use `smith-result`.
