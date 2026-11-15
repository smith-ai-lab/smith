---
type: specification
title: Generics and traits
description: Type parameters, traits over one or more types, impl, operators as trait functions, and derive.
---

# Generics and traits

Type parameters go in brackets after a name. A trait is a set of functions over one or more types, and an `impl` supplies them for particular types. A trait parameter marked `out` is determined by the others: once the two operand types of `add` are known, its result type is fixed. A bound such as `T: Add` requires an implementation in which every parameter is `T`.

```
enum Option[T] {
  some: T
  none
}

trait Add[A, B, out C] {
  fn add(a: A, b: B) -> C
}

impl Add[Rect, Rect, Rect] {
  fn add(a: Rect, b: Rect) -> Rect { Rect(w: a.w + b.w, h: a.h + b.h) }
}

fn twice[T: Add](x: T) -> T { x + x }

big = twice(unit)
both = add(unit, unit)
```

- A trait function is called like any other function, as in `add(unit, unit)`. It is qualified, as in `Add.add`, only when two names clash.
- Operators are trait functions: `a + b` means `Add.add(a, b)`. The arithmetic operators are `+`, `-`, `*`, `/`, and `^` for a power.
- A trait has at most one implementation for each choice of its types that are not `out`, and calls are specialized at compile time, so traits cost nothing at run time.

  The operands of an operator may differ in type, so a matrix times a vector, or a scalar times a struct of weights, is one more `impl` of `Mul`.
- `derive Add for Circle` generates an implementation field by field.
- A trait states the defaults of its functions' parameters, and an `impl` does not repeat them.

Next: [Literals and numbers](numbers.md)
