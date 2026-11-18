---
type: specification
title: Bounds and requires
description: What a function or an impl needs of its type parameters, written as a bound such as T: Named or as requires lines, and how an impl with type parameters is chosen.
status: partial
built:
  - "bounds such as `T: Plus`, `requires Trait[...]` on a function or an `impl`"
---

# Bounds and requires

A bound says that a type parameter must have an implementation of a trait. Inside the function, the trait's functions can be called on a value of that type.

## A bound on a type parameter

`show` takes any `T` that is `Named`, as [Traits](traits.md) defines it.

```
fn show[T: Named](x: T) -> str { name_of(x) }
```

`show(Cat(name: "Tom"))` is `"Tom"`. A call with a type that has no `impl` of `Named` is refused where it is called.

```
fn main() ! console {
  print(show(Cat(name: "Tom")))
  print(show(Bird(name: "Tweety")))
}
```

The error is "there is no implementation of `Named` for `Bird`, which `show` needs", and it points at the call of `show` with a `Bird`, not at `name_of` inside `show`.

## requires

A bound `T: Named` is the short form of a `requires` line, which is written after a function's signature and before its body. It names an implementation that must exist for the types the function is used with.

```
fn show[T](x: T) -> str
  requires Named[T]
{
  name_of(x)
}
```

This is the same function as the one above, and the shorter form is the one that is written when there is one type. A `requires` line may name several types, which a bound cannot. This function needs a `Pair` of its two types.

```
trait Pair[A, B] {
  fn join(a: A, b: B) -> str
}

impl Pair[bool, str] {
  fn join(a: bool, b: str) -> str { b }
}

fn show_both[A, B](a: A, b: B) -> str
  requires Pair[A, B]
{
  join(a, b)
}
```

`show_both(true, "pair")` is `"pair"`. With the two types the other way, `show_both("a", true)` is refused: "there is no implementation of `Pair` for `str` and `bool`, which `show_both` needs".

A function may have several `requires` lines. The functions of each are called inside it.

```
trait Plus[A, B, out C] {
  fn plus(a: A, b: B) -> C
}

impl Plus[bool, bool, bool] {
  fn plus(a: bool, b: bool) -> bool { a or b }
}

fn sum3[A, B](x: A, y: B, z: B) -> B
  requires Plus[A, B, B]
  requires Plus[B, B, B]
{
  plus(plus(x, y), z)
}
```

`sum3(false, false, true)` is `true`. `out`, which marks `C` in `Plus`, is the subject of [`out` parameters](out-parameters.md).

## An impl with type parameters

An `impl` takes type parameters after the word `impl`, and its head is a pattern over them. It stands for every choice of them that its `requires` lines allow. `Box[T]` is `Show` for each `T` that is `Show`.

```
trait Show[T] {
  fn show(x: T) -> str
}

struct Box[T] {
  item: T
}

impl Show[str] {
  fn show(x: str) -> str { x }
}

impl[T] Show[Box[T]]
  requires Show[T]
{
  fn show(x: Box[T]) -> str { show(x.item) }
}
```

For `b: Box[str] = Box(item: "inside")`, `show(b)` is `"inside"`, and a box of a box of a `str` is shown the same way. A `Box[i64]` is refused, as `i64` has no `Show` here: "there is no implementation of `Show` for `Box[i64]`".

A use that two `impl`s fit is refused. Here a `Box[i64]` fits both of the `impl`s of `Box[T]`, as `i64` has `Show` and `Ord`.

```
trait Show[T] {
  fn show(x: T) -> str
}

struct Box[T] {
  item: T
}

impl Show[i64] {
  fn show(x: i64) -> str { "n" }
}

impl[T] Show[Box[T]]
  requires Show[T]
{
  fn show(x: Box[T]) -> str { show(x.item) }
}

impl[T] Show[Box[T]]
  requires Ord[T]
{
  fn show(x: Box[T]) -> str { "ordered" }
}

fn main() ! console {
  b: Box[i64] = Box(item: 1)
  print(show(b))
}
```

This is refused: "`impl Show[Box[T]]` at line 13 and `impl Show[Box[T]]` at line 19 both fit this use".

## The forms

| Written | Is |
| --- | --- |
| `fn f[T: Named](x: T)` | `T` must have an implementation of `Named` |
| `requires Named[T]` | The same, as a line after the signature |
| `requires Pair[A, B]` | An implementation over several types must exist |
| `impl[T] Show[Box[T]]` | An `impl` for every `T` its `requires` lines allow |
| `impl[T] Show[Box[T]] requires Show[T]` | The same, only for a `T` that is `Show` |

Next: [`out` parameters](out-parameters.md)
