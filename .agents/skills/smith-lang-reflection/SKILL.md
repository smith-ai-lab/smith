---
name: smith-lang-reflection
description: "Reflection on a struct in a generic's body: `fields(T)` and `fields(typeof(E))` (a compile-time list of the named fields), `countof(fields(T))`, `fields(T)[i].name`, `x.[fields(T)[i]]`, `x->[...]` and `typeof(T.[...])`, so one function prints, compares or visits any struct. Load when a generic loops over the members of a struct, or on 'fields(T)', 'is not a struct', 'is a union; its members share their storage', 'has a member that is an anonymous struct or union', 'needs the compiler to read the struct'."
---

# Reflection: `fields(T)`

C has no way to write one function over the members of any struct short of a macro per struct. `fields(T)` is a compile-time list like `rest`, and every use is plain member access, so nothing is left for run time.

```smith
module reflect;

export typedef struct {
    f32 mass;
    i32 id;
    f32 charge;
} Particle;

export f64 sum_floats(constexpr type T, T *x) {             // any struct
    f64 total = 0;
    for constexpr (usize i = 0; i < countof(fields(T)); i++) {
        if constexpr (is_float(typeof(x->[fields(T)[i]]))) {
            total += (f64)x->[fields(T)[i]];                // written as x->mass, then x->charge
        }
    }
    return total;
}

export const char *first_name(constexpr type T) {
    return fields(T)[0].name;                               // a string literal: "mass"
}
```

`sum_floats(Particle, &p)` adds `mass` and `charge` and skips `id`. They work in the body of a generic in a module, with a compiler (`smith cc --smith-lower` cannot write them).

## Rules

- **`fields(T)`** (`constexpr type T`) or **`fields(typeof(E))`** (E a parameter, `auto x` or `T x`, or an expression of one: `*p`, `p->in`) is the named fields of E's struct, in order. **`countof(fields(T))`** is how many (a constant: it bounds a `for constexpr` and a `static_assert`); **`fields(T)[i]`** is one, `i` a constant (a literal, the loop variable, `countof(...) - 1`).
- **`fields(T)[i].name`** is the name as a string literal. **`x.[fields(T)[i]]`** and **`x->[...]`** are the member, read or written. **`typeof(T.[fields(T)[i]])`** (or `typeof(x->[...])`) is the field's type, in a condition too: `is_float(...)`, `is_record`, `is_array`, `is_pointer`, `typeof(...) == char *`.
- A field may be a nested struct (write a generic that calls itself on it), a fixed array (`is_array`; it decays to a pointer when passed, so pass `sizeof(a.[f]) / sizeof(a.[f][0])` too) or a bit-field (read and written; `&x.[f]` is C's own error). A tuple's fields are named "0", "1".
- An argument of a call to a generic that holds `x.[f]` (or any expression of a parameter) is typed with the parameters written as values of their types; a loop variable or an `auto` local cannot be one, so a generic that visits a nested struct passes `piece.[f]` itself. `format::to` writes a struct as `{m = 1, b = 2}` this way, and `test::check_equal(a, b)` compares field by field and names the first that differs (`a.pair.m = 1`, `b.pair.m = 2`). A generic cannot call a generic defined after it, so a function that visits a struct calls itself.
- **Doc comments** (issue #287): `fields(T)[i].doc` is the field's doc comment and `doc(T)` the type's (`T` or `typeof(x)`), string literals like `.name`, empty (`""`) when there is none. A doc comment is a `///` line or `/** */` block directly above a struct field, an enum constant or the type (above `typedef` or `export typedef`); `//`, `/* */` and a comment with code between it and the declaration are not. The markers are stripped (one space after each, trailing white space, the whole trimmed) and the lines joined with a newline: `/// a` and `/// b` give "a\nb". A module's header keeps them, so an instance in another module reads them. At most 1023 bytes (longer is an error).
- **Enums**: `fields(E)` over a plain enum lists its constants (`.name` as written, `.doc`, `.type` the enum; `x.[f]` is an error), `doc(E)` the enum's. A payload enum lists its variants; their docs are not read yet (empty).
- `fields(T)` as a value (`constexpr Field f`, `f.name`, `f.type`), slice and array fields: `smith-lang-reflection-field`.

## Errors

At the use, naming the type:

- ``` `fields(T)`: `U` is a union; its members share their storage, so it has no list of fields ```
- ``` `fields(T)`: `__INT32_TYPE__` is not a struct ```
- ``` `fields(T)[5]` is out of range: the type has 2 fields ``` (or an index that is no constant)
- a struct with an anonymous struct or union member; a raw lowering says it needs the compiler.

Generics: `smith-lang-generics`; loops: `smith-lang-rest`. Reference: [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Reflection").
