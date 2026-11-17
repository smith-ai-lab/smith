---
type: specification
title: Generics and traits
description: Type parameters, traits over one or more types, impl, operators as trait functions, and derive.
status: partial
built: "type parameters, traits with `out` parameters, `impl`, bounds such as `T: Plus`, calls of trait functions with defaults, `==` and `!=` through the `Eq` every `str`, struct, enum, tuple, `bool`, `()`, and integer type is given, and `<`, `<=`, `>`, and `>=` through the `Ord` every integer type has"
---

# Generics and traits

Type parameters go in brackets after a name. A trait is a set of functions over one or more types, and an `impl` supplies them for particular types. A trait parameter marked `out` is determined by the others: once the two operand types of `add` are known, its result type is fixed. A type parameter may have a default, written with `=` as a function's parameter writes one. A default may name a parameter before it, and parameters with defaults come after those without. So `Add[Rect]` means `Add[Rect, Rect, Rect]`, and a bound such as `T: Add` means `Add[T]`.

```
enum Option[T] {
  some: T
  none
}

trait Add[A, B = A, out C = A] {
  fn add(a: A, b: B) -> C
}

impl Add[Rect] {
  fn add(a: Rect, b: Rect) -> Rect { Rect(w: a.w + b.w, h: a.h + b.h) }
}

fn twice[T: Add](x: T) -> T { x + x }

big = twice(unit)
both = add(unit, unit)
```

A requirement about more than one type is written with `requires`, on a line after a function's signature or an `impl`'s head. It names an implementation that must exist for the types the function or the `impl` is used with, and inside, that implementation's functions can be called. `T: Add` is the short form of `requires Add[T]`.

```
fn sum3[A, B](x: A, y: B, z: B) -> B
  requires Add[A, B, B]
  requires Add[B]
{
  x + y + z
}
```

- A trait function is called like any other function, as in `add(unit, unit)`. It is qualified, as in `Add.add`, only when two names clash.
- Operators are trait functions: `a + b` means `Add.add(a, b)`. The arithmetic operators are `+`, `-`, `*`, `/`, and `^` for a power.
- A trait has at most one implementation for each choice of its types that are not `out`, and calls are specialized at compile time, so traits cost nothing at run time. An `impl` with type parameters stands for every choice of them that its `requires` lines allow, and a use that two implementations fit is an error.

  The operands of an arithmetic operator may differ in type, so a number times an array, or a number times a struct of weights, is one more `impl` of `Mul`.
- Comparisons are trait functions too. `a == b` means `Eq.eq(a, b)` and `a != b` means `not (a == b)`. `<`, `<=`, `>`, and `>=` come from `Ord`, whose one function says how two values stand. The two sides may differ in type where an implementation relates them, as two integer types are related under [Conversion](conversion.md). A comparison always gives one `bool`, on arrays too: `x == y` says whether two arrays are the same, and the comparison of each pair of elements is `for i { x[i] == y[i] }`.

  ```
  trait Eq[A, B = A] {
    fn eq(a: A, b: B) -> bool
  }

  enum Order {
    less
    equal
    greater
  }

  trait Ord[A, B = A] {
    fn compare(a: A, b: B) -> Order
  }
  ```
- Both traits keep their laws for every type, so code written over them is right for every type. Every value equals itself, every two values of a type with `Ord` can be ordered, and `compare` gives `equal` exactly when `==` is true. A float that is not a number therefore equals itself and comes after every other float, and `is_nan` says whether a float is one.
- Text is ordered by its bytes. The `Ord` a type is given orders a struct by its fields in the order they are written, a tuple and an array by their elements in order, and an enum by the order of its cases and then by what a case holds.
- `Eq` and `Ord` are given automatically, as [`Copy`](ownership.md) is: a struct, an enum, a tuple, or an array has each unless one of its parts lacks it, and `without Ord for T` takes one away. A type's own `impl` replaces the one it was given.
- `derive Add for Circle` generates an implementation field by field.
- A trait states the defaults of its functions' parameters, and an `impl` does not repeat them.

Next: [Literals and numbers](numbers.md)
