---
name: smith-lang-reflection
description: "Reflection on a struct in a generic's body: `fields(T)` and `fields(typeof(x))` (a compile-time list of the named fields), `countof(fields(T))`, `fields(T)[i].name`, `x.[fields(T)[i]]`, `x->[...]` and `typeof(T.[...])`, so one function prints, compares or visits any struct. Load when a generic loops over the members of a struct, or on 'fields(T)', 'is not a struct', 'is a union; its members share their storage', 'has a member that is an anonymous struct or union', 'is used as `fields(T)[i].name`', 'is out of range: the type has'."
---

# Reflection: `fields(T)`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-reflection/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. `auto... rest` and `for constexpr`: `smith-lang-rest`; generics: `smith-lang-generics`. The long reference is [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Reflection").

Code that should work for any struct is written once, as regular C run at compile time: `fields(T)` is a compile-time list like `rest`, and every use is written as plain member access, so nothing is left for run time. They work in the body of a generic in a module, with a compiler (the stage0 and a bootstrapped module cannot write them).

- **`fields(T)`** (T a `constexpr type T` parameter) or **`fields(typeof(x))`** (x an `auto x` or `T x` parameter) is the named fields of that struct, in order. **`countof(fields(T))`** is how many (a constant, so it bounds a `for constexpr` and a `static_assert`); **`fields(T)[i]`** is one, `i` a constant (a literal, the loop variable, `countof(...) - 1`).
- **`fields(T)[i].name`** is the field's name as a string literal. **`x.[fields(T)[i]]`** and **`x->[fields(T)[i]]`** are the member (`x.m`, `x->m`), read or written. **`typeof(T.[fields(T)[i]])`** (or `typeof(x->[...])`) is the field's type, in an `if constexpr` condition too: `is_float(typeof(x->[fields(T)[i]]))`, `is_record`, `is_array` (new), `is_pointer`, `typeof(...) == char *`.
- A field may be a nested struct (`fields` of its type visits it: write a generic that calls itself), a fixed array (`is_array`; it decays to a pointer when passed, so pass `sizeof(a.[f]) / sizeof(a.[f][0])` too) or a bit-field (read and written; its `typeof` is the integer type, and `&x.[f]` is C's own error). `[..]` fields are #82.
- Errors, at the use, naming the type: a **union**, a struct with an **anonymous struct or union member**, a type that **is not a struct**, an index **out of range**, an index that is no constant. A raw lowering says it needs the compiler.

```smith fragment
export void print_fields(constexpr type T, T *x) {              // any struct
    for constexpr (usize i = 0; i < countof(fields(T)); i++) {
        if constexpr (is_float(typeof(x->[fields(T)[i]]))) {
            c::printf("%s = %g\n", fields(T)[i].name, (f64)x->[fields(T)[i]]);
        } else if constexpr (is_integer(typeof(T.[fields(T)[i]]))) {
            c::printf("%s = %lld\n", fields(T)[i].name, (long long)x->[fields(T)[i]]);
        }
    }
}
```

An argument of a call to a generic that holds `x.[f]` (or any expression of a parameter: `*p`, `a.m`) is typed with the parameters written as values of their types, since the instance's parameters are not declared while its body is written; a local of the body cannot be one (a loop variable `j`, `auto y = ...`), so a generic that visits a nested struct or an array passes `piece.[f]` itself, and walks an array by moving its pointer (`put(w, *first); first += 1;`). `format::to` writes a struct as `{m = 1, b = 2}` this way, and `test::check_equal(a, b)` compares two structs field by field and names the first that differs (`a.pair.m = 1`, `b.pair.m = 2`). A generic cannot call a generic defined after it, so a function that visits a struct and its fields calls itself (the instance for the field's type).
