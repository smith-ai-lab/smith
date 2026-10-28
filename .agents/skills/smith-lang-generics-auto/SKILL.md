---
name: smith-lang-generics-auto
description: "How `auto` reads its type in a generic and what converts to it: the argument's exact compiler type (any expression), `cannot tell the type of`, and the conversion rule for later arguments (a value-changing conversion is an error at the call: `the argument 300 of check_equal converts to u8`), literals judged by value. Load on 'cannot tell the type of', 'converts to ..., the type of its first argument', or when an argument of a generic has the wrong type."
---

# What `auto` takes and what converts

```smith
module check;

export bool same(auto a, typeof(a) b) {     // b converts to the type of a, unless that changes its value
    return a == b;
}

export bool demo() {
    u8 small = 7;
    return same(small, 7)            // 7 fits u8: fine
        && same(1.5f, 1.5f)          // 0.5 and 1.5f are f32; 0.1 is not
        && same(small, 3 + 4);       // an expression of literals is judged by value
    // same(small, 300) is an error: 300 does not fit u8
}
```

## The type of an argument

The lowering has the parser read `__typeof__(<the argument>)` in the scope the call is in, so any expression has its exact type (`((u8 *)block)[n - 1]`, a member of an anonymous union, `a++`, a statement expression, a call to another generic, a constant of its own type), a local declared by an earlier statement counts, and nothing is evaluated. Arrays are pointers and qualifiers are dropped.

- An argument the compiler does not accept (a name it does not know) is ``cannot tell the type of `x`, the argument of `f`: <the compiler's message>; give it a type with a cast or a variable``.
- Inside a generic's body, an argument that is a parameter of that generic (`value_of(a)`), a local declared with a type and a name (`i32 n = 3; twice(n)`), a parameter such as `typeof(proto) *p` (`twice(*p)`) or `rest[k]` in an expression (`twice(rest[k] + 1)`) has the instance's type; a local declared `auto` does not (give it a type).
- A later argument the compiler folds to a number is judged by its value and its own type.

## Conversions

A later argument converts to `typeof(a)` by C's rules, except that a conversion that changes the value is an error at the call, with no warning flag needed: ``the argument `300` of `check_equal` converts to u8, the type of its first argument `small`, and changes its value; cast it, `(u8)300`, or the first argument, `(i32)small` ``.

- **By value**: a literal and an expression of literals (`1 << 4`, `flag_a | flag_b`), `constexpr` integers (the file's, or a C header's through `c::`, of any signedness: `c::bpe_failed`, a `uint32_t` of all ones, is 4294967295 and fits a `u32`) and enumerators (`c::Style_Count - 1`). `0.1` is not an `f32`; `0.5` and `0.1f` are; `-1` is not a `u32`.
- **By type**, for any other argument: its range must fit the target's (`u8` to `u32` and `i32` to `f64` yes; `u32` to `u8`, `i32` to `u32`, `i32` to `f32`, `f64` to `f32` no).
- **The element of a shaped parameter** is the same: with `usize find(const auto xs[:], typeof(xs[0]) v)`, `find(bytes, 300)` for a `u8` slice is the error (``the argument `300` of `find` converts to u8, the element type of its first argument `bytes`, and changes its value; cast it, `(u8)300` ``); `typeof(*p)` of `auto *p` the same.
- Bools, enums and pointers convert by C's rules; an argument whose type cannot be said is not judged. Fix an error with the cast the message names.

Generics: `smith-lang-generics`.
