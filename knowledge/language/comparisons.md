---
type: specification
title: Comparisons
description: == and != through the Eq trait, <, <=, >, and >= through the Ord trait, and the laws every implementation keeps.
status: partial
built: "`==` and `!=` through the `Eq` every `str`, struct, enum, tuple, `bool`, `()`, and integer type is given, and `<`, `<=`, `>`, and `>=` through the `Ord` every integer type has"
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
struct Cat {
  name: str
  age: i64
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

A type's own `impl` of `Eq` says when two of its values are equal.

```
impl Eq[Cat] {
  fn eq(a: Cat, b: Cat) -> bool { a.age == b.age }
}
```

Now `tom == Cat(name: "Rex", age: 3)` is `true`.

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
fn smaller(a: i64, b: i64) -> bool { a < b }
```

The two sides may differ in type where an implementation relates them, as two integer types are related under [Conversion](conversion.md).

## Comparing arrays

A comparison gives one `bool`, on arrays too: `x == y` says whether two arrays are the same. The comparison of each pair of elements is `for i { x[i] == y[i] }`, as [Arrays](arrays.md) and [`for`](for.md) show.

## What the traits promise

Both traits keep their laws for every type, so code written over them is right for every type.

- Every value equals itself.
- Every two values of a type with `Ord` can be ordered.
- `compare` gives `equal` exactly when `==` is true.

A float that is not a number therefore equals itself and comes after every other float, and `is_nan` says whether a float is one.

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
