---
name: smith-lang-slice-pointers
description: "A pointer to a slice as C's pointer to an array: `const char (*text)[:]` (`*text` is the slice, `(*text)[i]`, `(*text)[a:b]`, `countof(*text)`, `*text = (*text)[n:]`), `typeof(const char[:]) *text`, a typedef'd `Text *text`, passing `&s`, a cursor such as `take_line(&s, &line)`. Load when a .smith file takes or returns a slice by pointer, writes `(*p)[:]`, or on 'converting a pointer to a slice of ... discards qualifiers'."
---

# A pointer to a slice

A slice is C's array declarator with a run-time size, so a pointer to one is C's pointer to an array, as `int (*p)[10]`. It lets a function advance the caller's slice, as a cursor does.

```smith
module lines;

export bool take_line(const char (*text)[:], const char (*line)[:]) {
    if (countof(*text) == 0) return false;
    usize n = 0;
    while (n < countof(*text) && (*text)[n] != '\n') n++;   // *text is the slice
    *line = (*text)[0:n];
    *text = (*text)[n < countof(*text) ? n + 1 : n:];       // advance the caller's slice
    return true;
}

export usize count_lines(const char all[:]) {
    const char s[:] = all;
    const char line[:] = s;
    usize n = 0;
    while (take_line(&s, &line)) n++;                        // line is "a", then "b", ...
    return n;
}
```

## Rules

- **Three spellings, one type:** `const char (*text)[:]`, `typeof(const char[:]) *text` and, with `typedef typeof(const char[:]) Text;`, `Text *text`. More stars (`(**pp)[:]`), a qualified pointer (`(*const p)[:]`) and more dimensions (`const f32 (*m)[:][2]`) work as in C.
- **At a call `&s` passes unchanged.** C23's rule for pointers to arrays: a pointer to `char[:]` converts implicitly to a pointer to `const char[:]` (a call and an initializer; the lowering casts, the structs differ only in the element's `const`). The same holds for a pointer variable. Not converted: an assignment `cp = p;` or a `return` of a pointer to a slice with another qualifier (clang reports a mismatch).
- **A typedef of a slice type is the slice everywhere:** `Text s = "abc";`, `Text r = raw;` (an array), `s = "xy";` and `typeof(const char[:]) s = "abc";` work as `const char s[:] = "abc";` does. `typedef const char str[:];` (C's typedef of an array declarator) is the same: `str s = "abc";`, `str *p = &s;`, a `str` parameter, result, struct field or generic argument, exported and used as `text::str`.
- **Initializer lists:** a slice member takes a slice expression in `struct Pair p = {.key = "k", .value = "x"};`, by position, nested and in `(struct Pair){...}`, as in an assignment (a string, an array or a slice converts). Braces for a slice, and a deeper designator (`.a.b =`, `[i] =`) in a struct with a slice member, are errors, never an empty slice; so are a slice member in an array of structs, in a struct with an anonymous member and in an initializer of over 200 tokens (assign the slice after the declaration).
- **In a generic** the same pointer has `auto` as its element: `export usize skip(const auto (*xs)[:], typeof((*xs)[0]) stop)` with `*xs = (*xs)[i:]`, `(*xs)[i]` and `countof(*xs)` in the body, called as `skip(&line, '=')` (`typeof((*xs)[0:0])` is the slice, `typeof((*xs)[0])` its element). One star; a result `option(typeof((*xs)[0:0]))` is the struct of `option(struct smith_array_..._n)`, so a caller names it with that struct (`smith-lang-generics`).
- Underneath: a pointer to the slice's struct (`struct smith_array_const_char_n *`), so C passes `&s` of a `struct smith_array_const_char_n s`.
- Not yet: the pointee's brackets carrying qualifiers (`(*p)[const :]`); write `typeof(...)` for a pointer to a slice that is itself const.

## Errors

- ``converting a pointer to a slice of `const char` to a pointer to a slice of `char` discards qualifiers``

Slices: `smith-lang-arrays`; qualifiers: `smith-lang-slice-qualifiers`.

A generic's `option(typeof((*xs)[0:0]))` result is named by a caller as `option(Text)` for a `typedef const char Text[:]`.
