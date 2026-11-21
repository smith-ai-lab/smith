---
type: specification
title: Comparisons
description: == and != through the Eq trait, <, <=, >, and >= through the Ord trait, and the laws every implementation keeps.
status: partial
built:
  - "An `impl Eq[T]` that a program writes is refused"
  - "`==`, `!=`, `<`, `<=`, `>`, and `>=` between two floats of one type, which say a float that is not a number equals itself and is above every other, and `-0.0` another value than `0.0` and below it, on every target"
  - "`==` and `!=` through the `Eq` every `str`, struct, enum, tuple, `bool`, `()`, and integer type is given, and `<`, `<=`, `>`, and `>=` through the `Ord` every `str`, struct, enum, tuple, `bool`, `()`, and integer type is given"
---

# Comparisons

A comparison is a call of a trait function, and it always gives one `bool`. `==` and `!=` ask whether two values are equal, and `<`, `<=`, `>`, and `>=` how they stand.

## Equal and not equal

`a == b` means `Eq.eq(a, b)`, and `a != b` means `not (a == b)`. `Eq` has one function.

```
trait Eq[A, B = A] {
  fn eq(a: A, b: B) -> bool
}
```

A text, a struct, an enum, a tuple, a `bool`, `()`, and a whole number each have `Eq`, so they are compared with `==`.

```
use std.console (console, print)

struct Cat {
  name: str
  age: Int
}

fn word(b: bool) -> str {
  if b { "yes" } else { "no" }
}

fn main() ! console {
  tom = Cat(name: "Tom", age: 3)
  print(word("abc" == "abc"))
  print(word((1, "a") == (1, "a")))
  print(word(tom == Cat(name: "Tom", age: 4)))
  print(word(tom != Cat(name: "Tom", age: 4)))
}
```

This prints `yes`, `yes`, `no`, and `yes`.

The two sides must have the same type unless an `impl` relates them. `tom == 3` is refused: "this is a number, where a `Cat` is needed".

`==` means that two values are the same value, and it means nothing else. So a program does not write `Eq` for a type: the `Eq` a type has is the one it is given, which compares it part by part, as [Traits a type is given](automatic-traits.md) describes. An `impl Eq[Cat]` is refused.

```
impl Eq[Cat] {
  fn eq(a: Cat, b: Cat) -> bool { a.age == b.age }
}
```

This is refused: "`==` means that two values are the same value, so `Eq` is given to a type and is not written; a looser comparison is a function with its own name".

A comparison that is looser than "the same" is a function with a name of its own.

```
fn same_age(a: Cat, b: Cat) -> bool { a.age == b.age }
```

`same_age(tom, Cat(name: "Rex", age: 4))` is `true`, and `tom == Cat(name: "Rex", age: 4)` is `false`, as they are two cats. A type whose value can be written in more than one way, as a fraction can, keeps one way, so that the same value is always the same parts.

This is what lets a [proof](statements.md) use one of two equal values where the other stood. It holds of the floats too: a float that is not a number equals itself, as the laws below say. And `0.0` and `-0.0` are two values, as dividing by one gives infinity and by the other its negative, so `0.0 == -0.0` is `false`.

## Less and greater

`<`, `<=`, `>`, and `>=` come from `Ord`, whose one function says how two values stand. It gives an `Order`.

```
enum Order {
  less
  equal
  greater
}

trait Ord[A, B = A] {
  fn compare(a: A, b: B) -> Order
}
```

Each of the four is one answer of `compare`, or all but one:

| Written | Is true when `compare(a, b)` is |
| --- | --- |
| `a < b` | `.less` |
| `a <= b` | not `.greater` |
| `a > b` | `.greater` |
| `a >= b` | not `.less` |

So a type writes `compare` once and has all four, and they cannot disagree with each other. `!=` is the same for `Eq`: it has no function of its own and is always the opposite of `==`.

A whole number has `Ord`, so it is compared with `<`.

```
fn smaller(a: Int, b: Int) -> bool { a < b }
```

The two sides may differ in type where an implementation relates them, as two integer types are related under [Conversion](conversion.md).

## Comparing arrays

A comparison gives one `bool`, on arrays too: `x == y` says whether two arrays are the same. The comparison of each pair of elements is `for i { x[i] == y[i] }`, as [Arrays](arrays.md) and [`for`](for.md) show.

## What the traits promise

Both traits keep their laws for every type, so code written over them is right for every type.

- Every value equals itself.
- Every two values of a type with `Ord` can be ordered.
- `compare` gives `equal` exactly when `==` is true.
- The order agrees with itself: when `a` is below `b` and `b` is below `c`, `a` is below `c`, and `a` is below `b` exactly when `b` is above `a`.

`Eq` keeps its law by what it is, as a program does not write it. The `Ord` a type is given keeps the laws of an order by how it is made, part by part.

A program may write `Ord` for a type whose order is not part by part. A fraction kept in lowest terms is one: a half is above a third, though its parts are not.

```
impl Ord[Fraction] {
  fn compare(a: Fraction, b: Fraction) -> Order { compare(a.top * b.bottom, b.top * a.bottom) }
}
```

An `Ord` that a program writes owes a proof of each law. A law is a function of the trait whose result is the [statement](statements.md), so an `impl` supplies its proof as it supplies `compare`, and code written over `Ord` gets a law by calling it. There is no other place a law is written. The trait gains these functions when statements are built, and until then such an `Ord` is [taken on trust](trust.md#what-is-still-taken-on-trust).

An order that calls two different values `equal` is not an `Ord`. People put in order of their ages are sorted by a key, as `sort_by(people, age)`, and two people of one age are still two.

A float that is not a number therefore equals itself and comes after every other float, and `is_nan` says whether a float is one. `-0.0` comes just before `0.0`, as two values that are not equal cannot compare `equal`. Two floats that should be the same number after rounding are compared within a tolerance by a function of the library, not with `==`.

## The forms

| Written | Is |
| --- | --- |
| `a == b` | `Eq.eq(a, b)` |
| `a != b` | `not (a == b)` |
| `a < b` | `Ord.compare(a, b)` is `.less` |
| `a <= b` | `Ord.compare(a, b)` is not `.greater` |
| `a > b` | `Ord.compare(a, b)` is `.greater` |
| `a >= b` | `Ord.compare(a, b)` is not `.less` |
| `x == y` on arrays | One `bool`, true when the arrays are the same |

Next: [Traits a type is given](automatic-traits.md)
