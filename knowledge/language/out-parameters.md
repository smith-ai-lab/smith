---
type: specification
title: out parameters
description: A trait parameter marked out is determined by the others, so a call's result type follows from its operands, and a trait parameter may have a default.
status: partial
built:
  - "traits with `out` parameters"
---

# out parameters

A trait parameter marked `out` is determined by the others. Once the types of the other parameters are known, the type of the `out` parameter is fixed.

## A trait with an out parameter

`Join` takes a `Cat` and a `Bird`, and gives a `C`.

```
struct Cat {
  name: str
}

struct Bird {
  name: str
}

trait Join[A, B, out C] {
  fn join(a: A, b: B) -> C
}

impl Join[Cat, Bird, str] {
  fn join(a: Cat, b: Bird) -> str { a.name }
}
```

`A` and `B` are the types of the two arguments, and `C` is the type of the result. The `impl` names the three types, in the order the trait lists them. `join(tom, tweety)` is a `str`, with no type written for it, as the `impl` for a `Cat` and a `Bird` gives a `str`.

A function that is bound by a trait with an `out` parameter gets the type of its result from the bound.

```
trait Plus[A, B, out C] {
  fn plus(a: A, b: B) -> C
}

impl Plus[bool, bool, bool] {
  fn plus(a: bool, b: bool) -> bool { a or b }
}

fn twice[T: Plus](x: T) -> T { plus(x, x) }
```

`twice(true)` is `true`.

## At most one result

A trait has one `impl` for each choice of the types that are not `out`, and the `out` type is the one that `impl` gives. That is why it is known. A second `impl` of `Join` for a `Cat` and a `Bird` that gives another type is refused, as the result of `join(tom, tweety)` would then not be one type.

```
impl Join[Cat, Bird, Cat] {
  fn join(a: Cat, b: Bird) -> Cat { a }
}
```

The error is "`Join` is already implemented for `Cat` and `Bird`".

## A default for a trait parameter

A trait's parameters may have defaults, as [Type parameters](type-parameters.md) shows for a type, and `out` may be put in front of one. So `Add` can be written for the common case of one type.

```
trait Add[A, B = A, out C = A] {
  fn add(a: A, b: B) -> C
}
```

`Add[Rect]` means `Add[Rect, Rect, Rect]`: `B` is `Rect`, as `B = A`, and `C` is `Rect`. A bound that names the trait alone, such as `T: Add`, means `Add[T]`.

```
fn twice[T: Add](x: T) -> T { add(x, x) }
```

The operators of [Operators](operators.md) are written over `Add` and the traits like it.

## The forms

| Written | Is |
| --- | --- |
| `trait Join[A, B, out C]` | A trait whose `C` follows from `A` and `B` |
| `impl Join[Cat, Bird, str]` | The `impl` for those operands, which fixes `C` |
| `trait Add[A, B = A, out C = A]` | A trait whose `B` and `C` are `A` unless written |
| `Add[Rect]` | `Add[Rect, Rect, Rect]` |
| `T: Add` | `requires Add[T]` |

Next: [Operators](operators.md)
