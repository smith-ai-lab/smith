---
name: smith-lang-concepts
description: "Concepts in .smith: a question of a type (Int, Float, Signed, Unsigned, Num, Pointer, Enum, Struct, Array, Slice) written where `auto` goes (`Num x`, `Int n = 3;`), so a wrong argument is an error at the call, and the same names as functions in `if constexpr (Int(T))`. Load when a parameter or local is written with one of those names, or on 'is not `Num`', 'is not `Int`' and the like."
---

# Concepts: `Num x` instead of `auto x`

A concept is a question of a type that has a name. `auto` accepts every type; `Num` is `auto` that accepts only an integer or a floating type, and says so at the call that gets it wrong, not somewhere in the body.

```smith
module stats;

export typeof(x) twice(Num x) {        // an instance for each type of the argument, like `auto x`
    Int one = 1;                          // a local with an initializer: `auto`, once its type is of the concept
    return x + x + one - one;
}

export typeof(a) larger(Int a, typeof(a) b) {
    return a > b ? a : b;                     // each use is independent: the same type is `typeof(a)`
}
```

`stats::twice(2.5)` is an `F64`; `stats::twice("text")` is an error at the argument, the main thing first:

```
main.smith:12:24: error: `const char *` is not `Num`
stats.smith:3:24: note: `stats::twice` takes `Num x`
note: `Num(const char *)` is false: `Int(const char *)` and `Float(const char *)` are both false; `const char *` is a pointer type
```

## Rules

- **The concepts** are `Int` (not `Bool`, not an enum), `Float`, `Signed`, `Unsigned`, `Num` (integer or floating), `Pointer`, `Enum`, `Struct` (a struct or a union), `Array` (a fixed array) and `Slice` (`T[:]`). `Bool` is the one boolean type, so there is no concept for it. `is_integer`, `is_float`, `is_signed`, `is_bool`, `is_enum`, `is_record`, `is_pointer`, `is_array` and `is_slice` are the old names and still work until a release has the nouns.
- **A concept stands where `auto` does:** a parameter (`Num x`) and a local with an initializer (`Int n = 3;`). A name of the program's with the same spelling (a typedef, a variable) shadows the concept, as any C name does.
- **In a body** the same names are functions of a type: `if constexpr (Float(T))`, `static_assert(Int(kind))` (`smith-lang-type-values`).
- **The element of a declarator:** a concept stands where `auto` does in a parameter's slice, pointer or array: `const Num xs[:]`, `Float m[:][:]`, `const Int *p`, `Num xs[4]`. The declarator keeps its shape and qualifiers (`smith-lang-generics`); the concept judges the element (the pointee), each use is its own instance, and `typeof(xs[0])` or `typeof(*p)` names it in the rest of the signature (`Usize find(const Num xs[:], typeof(xs[0]) v)`). An argument of another shape is the shape error (``the parameter `xs` is a slice (`Num xs[:]`)``), one whose element is not of the concept is ``error: `F32` is not `Int`: the pointee of `&f` has that type ``, at the argument (a slice's: ``the elements of `ns` have that type``), with the same two notes. A local (`Num x = "a";`) is the error at the initializer and the why note.
- **Not yet:** a check of a local in a generic's body (it is `auto` there), a concept you write yourself as `bool name(constexpr Type T)` (only these ten stand for a type), and the stack of a long concept behind `smith cc -v`.
