---
name: smith-lang-reflection
description: "Reflection on a struct in a generic's body: `fields(T)` and `fields(typeof(x))` (a compile-time list of the named fields), `countof(fields(T))`, `fields(T)[i].name`, `x.[fields(T)[i]]`, `x->[...]` and `typeof(T.[...])`, and `fields(T)[i]` as a `Field` value (`constexpr Field f = fields(T)[i];`, `f.name`, `f.type`, passed to a function), so one function prints, compares or visits any struct. Load when a generic loops over the members of a struct, or on 'fields(T)', 'is not a struct', 'is a union; its members share their storage', 'has a member that is an anonymous struct or union', 'no member named in struct smith_field', 'needs the compiler to read the struct'"
---

# Reflection: `fields(T)`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-reflection/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. `auto... rest` and `for constexpr`: `smith-lang-rest`; generics: `smith-lang-generics`. The long reference is [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Reflection").

Code that should work for any struct is written once, as regular C run at compile time: `fields(T)` is a compile-time list like `rest`, and every use is written as plain member access, so nothing is left for run time. They work in the body of a generic in a module, with a compiler (the stage0 and a bootstrapped module cannot write them).

- **`fields(T)`** (T a `constexpr type T` parameter) or **`fields(typeof(x))`** (x an `auto x` or `T x` parameter) is the named fields of that struct, in order. **`countof(fields(T))`** is how many (a constant, so it bounds a `for constexpr` and a `static_assert`); **`fields(T)[i]`** is one, `i` a constant (a literal, the loop variable, `countof(...) - 1`).
- **`fields(T)[i].name`** is the field's name as a string literal. **`x.[fields(T)[i]]`** and **`x->[fields(T)[i]]`** are the member (`x.m`, `x->m`), read or written. **`typeof(T.[fields(T)[i]])`** (or `typeof(x->[...])`) is the field's type, in an `if constexpr` condition too: `is_float(typeof(x->[fields(T)[i]]))`, `is_record`, `is_array` (new), `is_pointer`, `typeof(...) == char *`.
- A field may be a nested struct (`fields` of its type visits it: write a generic that calls itself), a fixed array (`is_array`; it decays to a pointer when passed, so pass `sizeof(a.[f]) / sizeof(a.[f][0])` too) or a bit-field (read and written; its `typeof` is the integer type, and `&x.[f]` is C's own error). `[:]` fields are #82.
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

## `fields(T)` is an array of `Field`

`Field` is `struct { char name[:]; u64 type; }` (a `type` is a handle; `is_float(f.type)`, `f.type == f32`), defined once per file by the lowering when the file's text has `fields(` or `Field`, or it imports a generic that reflects. `fields(T)[i]` is an ordinary value: store it, pass it to a function (which runs in the compiler), return it, compare its parts. A name is `f.name` (a slice: `f.name.data`, `countof(f.name)` in an ordinary function), a tuple's fields are named "0", "1".

```smith fragment
bool is_floating(Field f) { return is_float(f.type); }          // ordinary C over a Field, defined in the same file
Field first_float(Field a, Field b) { return is_floating(a) ? a : b; }

f64 sum_floats(constexpr type T, T *x) {
    f64 total = 0;
    for constexpr (usize i = 0; i < countof(fields(T)); i++) {
        constexpr Field f = fields(T)[i];                      // or `constexpr auto f`
        if constexpr (is_floating(f)) total += (f64)x->[f];     // x.[f], x->[f], typeof(T.[f]), is_float(f.type)
    }
    return total;
}
```

- Fields are read by the compiler, so the function a condition calls (`is_floating`) must be defined in the file that instantiates the generic (a module's own functions are not visible to another file's instance, unless they are functions over `type`: `smith-lang-type-values`).
- A local `constexpr Field f = E;` stands for `E` in the conditions of its block; `constexpr Field g = first_float(fields(T)[0], fields(T)[1]);` works the same way. `fields(T)` alone is a `static constexpr Field[N]` (`const Field *all = fields(T);`).
- `fields(T)[i].name` stays a string literal. `typeof(x->[f])` of a bit-field stored in a variable is C's own error.
- `countof(f.name)` is not lowered inside a generic's body (use an ordinary function, or `f.name.count[0]`); `constexpr type k = f.type;` (a type value in a body) is not built.
