---
name: smith-lang-concepts
description: "Concepts in .smith: a function over a type (`Int`, `Float`, `Num`, `Signed`, `Unsigned`, `Pointer`, `Enum`, `Struct`, `Array`, `Slice`, `Any`, or one you write) named where a type goes (`Num x`, `Int n = 3;`, `Num... rest`), so a wrong argument is an error at the call. Load when a parameter or local is written with one of those names, on `Bool Name(constexpr Type T)`, `kind(T)`, or on '`Num(const char *)` is false'."
---

# Concepts: `Num x` instead of `auto x`

A concept is a function over a type that has a name. `Any` accepts every type; `Num` accepts only an integer or a floating type, and says so at the call that gets it wrong, not somewhere in the body.

```smith
module stats;

export typeof(x) twice(Num x) {               // an instance for each type of the argument
    Int one = 1;                              // a local with an initializer, once its type is of the concept
    return x + x + one - one;
}

export typeof(a) larger(Int a, typeof(a) b) {
    return a > b ? a : b;                     // each use is independent: the same type is `typeof(a)`
}
```

`stats::twice(2.5)` is an `f64`; `stats::twice("text")` is one line at the argument: ``error: `Num(const char *)` is false``.

## Rules

- **The standard concepts** are ordinary functions of `lib/smith/concepts.smith`, in scope in every module with no import: `Any`, `Int` (not `Bool`, not an enum), `Float`, `Num` (`Int` or `Float`), `Signed` and `Unsigned` (the sized types and C's integer types), `Pointer`, `Enum`, `Struct` (a struct or a union), `Array` (a fixed array) and `Slice` (`T[:]`). In a body they are called: `if constexpr (Float(T))`, `static_assert(Int(kind))` (`smith-lang-type-values`). `is_integer`, `is_float`, `is_signed`, `is_bool`, `is_enum`, `is_record`, `is_pointer`, `is_array` and `is_slice` are the old names and still work until a release has the new ones. A concept needs a `module` line.
- **A concept stands where `auto` does:** a parameter (`Num x`), a local with an initializer (`Int n = 3;`), the element of a range `for` and the rest pack (`Num... rest`: each argument is judged). A name of the program's with the same spelling (a typedef, a variable) shadows the concept, as any C name does.
- **The element of a declarator:** `const Num xs[:]`, `Float m[:][:]`, `const Int *p`, `Num xs[4]`. The declarator keeps its shape and qualifiers (`smith-lang-generics`); the concept judges the element (the pointee), each use is its own instance, and `typeof(xs[0])` or `typeof(*p)` names it in the rest of the signature (`usize find(const Num xs[:], typeof(xs[0]) v)`). An argument of another shape is the shape error; one whose element is not of the concept is ``error: `Int(f32)` is false``, at the argument.
- **`kind(T)`** is the one question of a type's kind (a `Kind`: `Kind::integer`, `floating`, `boolean`, `pointer`, `enumeration`, `structure`, `array`, `slice`, `other`), in a function over a type and in `if constexpr`: `if constexpr (kind(typeof(x)) == Kind::pointer)`. A union is `Kind::structure`. The standard concepts are written over it, so can yours.
- **Your own concept** is an ordinary function `Bool Name(constexpr Type T)` with a capitalized name; it runs in the compiler and is never instantiated. A parameter names it where a type goes (`Ordered x`, `const Ordered xs[:]`, `Ordered *p`, `Ordered... rest`), and the call runs the function on the argument's type: `Bool Ordered(constexpr Type T) { return Int(T) || Float(T); }`. A false answer is one line at the argument, `` `Ordered(const char *)` is false``, with no trace. It is known from its declaration on, in the module that declares it (an exported generic that names it carries it to its importers).
- **Not yet:** a check of a local in a generic's body (it is `auto` there), a trace behind `smith cc -v`, and naming the concept of an imported module in a parameter.
