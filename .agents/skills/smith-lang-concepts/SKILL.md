---
name: smith-lang-concepts
description: "Concepts in .smith: a question of a type (integer, floating, signed_integer, unsigned_integer, arithmetic, boolean, pointer, enumeration, structure, array, slice) written where `auto` goes (`arithmetic x`, `integer n = 3;`), so a wrong argument is an error at the call, and the same names as functions in `if constexpr (integer(T))`. Load when a parameter or local is written with one of those names, or on 'is not `arithmetic`', 'is not `integer`' and the like."
---

# Concepts: `arithmetic x` instead of `auto x`

A concept is a question of a type that has a name. `auto` accepts every type; `arithmetic` is `auto` that accepts only an integer or a floating type, and says so at the call that gets it wrong, not somewhere in the body.

```smith
module stats;

export typeof(x) twice(arithmetic x) {        // an instance for each type of the argument, like `auto x`
    integer one = 1;                          // a local with an initializer: `auto`, once its type is of the concept
    return x + x + one - one;
}

export typeof(a) larger(integer a, typeof(a) b) {
    return a > b ? a : b;                     // each use is independent: the same type is `typeof(a)`
}
```

`stats::twice(2.5)` is an `f64`; `stats::twice("text")` is ``error: `const char *` is not `arithmetic`: ... the parameter `x` is `arithmetic x` `` at the argument.

## Rules

- **The concepts** are `integer` (not `bool`, not an enum), `floating`, `signed_integer`, `unsigned_integer`, `arithmetic` (integer or floating), `boolean`, `pointer`, `enumeration`, `structure` (a struct or a union), `array` (a fixed array) and `slice` (`T[:]`). They are the C standard's nouns for the kinds of types; `is_integer`, `is_float`, `is_signed`, `is_bool`, `is_enum`, `is_record`, `is_pointer`, `is_array` and `is_slice` are the old names and still work until a release has the nouns.
- **A concept stands where `auto` does:** a parameter (`arithmetic x`) and a local with an initializer (`integer n = 3;`). A name of the program's with the same spelling (a typedef, a variable) shadows the concept, as any C name does.
- **In a body** the same names are functions of a type: `if constexpr (floating(T))`, `static_assert(integer(kind))` (`smith-lang-type-values`).
- **Not yet:** a concept in the element type (`const arithmetic xs[:]`), a pointer or an array parameter, and a check of a local in a generic's body (it is `auto` there).
