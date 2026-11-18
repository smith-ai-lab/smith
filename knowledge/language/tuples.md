---
type: specification
title: Tuples
description: Tuples that group values without names, patterns that take them apart and assign, and the empty tuple.
status: partial
built:
  - "tuples, and patterns that define and assign"
---

# Tuples

A tuple groups values without naming them. It is written as the values in parentheses, and its type is the types in parentheses.

```
fn sides(w: i32, h: i32) -> (i32, i32) { (w, h) }
```

`sides(3, 4)` is a tuple of two `i32`s. A tuple is taken apart by a pattern on the left of a binding: each name in the pattern is defined as the value in the same place.

```
fn main() ! console, alloc {
  (a, b) = sides(3, 4)
  print("{a} {b}")
}
```

This prints `3 4`. The values may have different types, and a tuple may have any number of them. Tuples nest, and a pattern nests the same way.

```
fn main() ! console, alloc {
  (n, word, flag) = (1, "one", true)
  ((a, b), c) = ((1, 2), 3)
  print("{n} {word} {flag} {a} {b} {c}")
}
```

This prints `1 one true 1 2 3`. A tuple is a value, so it can be bound to a name first and taken apart after.

```
pair = (1, "one")
(n, word) = pair
```

There is no `t.0`. A tuple's parts are reached only by a pattern, and `t.0` is refused: "expected a field's name, found the number `0`".

## A pattern must fit the tuple

A pattern has as many names as the tuple has values, and each is defined with the type of the value it takes.

```
(a, b, c) = sides(3, 4)
```

This gives "this pattern takes apart a tuple of 3, and it matches a `(i32, i32)`". A pattern on a value that is not a tuple gives the same message with that type, so `(a, b) = count()` where `count` returns an `i32` gives "this pattern takes apart a tuple of 2, and it matches a `i32`". A value of the wrong type inside the tuple is refused where it is written: `fn pair() -> (i32, str) { (1, 2) }` gives "this is a number, where text is needed".

## A pattern wherever a name is introduced

A tuple's pattern may stand wherever a name is introduced, and not only on the left of a binding: as a parameter of a function, as a parameter of a [lambda](lambdas.md), and as the name of a [`loop`](loop.md).

```
fn first((n, s): (i32, str)) -> i32 { n }

fn total(pairs: List[(str, i32)]) -> i32 {
  var sum = 0
  loop (name, count) in pairs { sum = wrapping_add(sum, count) }
  sum
}
```

`first((7, "x"))` is `7`. In `total`, each element of `pairs` is taken apart as the loop meets it. A lambda does the same: `map(pairs) fn((name, count)) { count }`.

Only a pattern that always matches may stand in these places: a name, and a tuple of such patterns. A pattern that names a case or a literal may not match, so it is written in a [`match`](match.md).

A parameter written as a pattern has no name of its own, so its argument is given by position. `first(t: (7, "x"))` is refused, as `first` has no parameter named `t`. The compiler does not build a pattern in these places, so no words are quoted.

## A `var` in a pattern

A name in a pattern is immutable, as any [binding](bindings.md) is. `var` before a name makes that one a [`var`](var.md).

```
fn main() ! console, alloc {
  (a, var b) = sides(3, 4)
  b = 9
  print("{a} {b}")
}
```

This prints `3 9`. Assigning to `a` is refused, as it is for any name that is not a `var`: "`a` is not a `var`, so it cannot be assigned to".

## A pattern that assigns

A pattern may name a `var` that is already defined, and then it assigns to it where it would otherwise define a new name. One pattern may define some names and assign to others. In `(total, rest) = split(total)`, `total` is a `var`, so it is assigned, and `rest` is new, so it is defined.

```
fn split(n: i32) -> (i32, i32) { (n, 1) }

fn main() ! console, alloc {
  var total = 5
  (total, rest) = split(total)
  print("{total} {rest}")
}
```

This prints `5 1`. The elements of the tuple on the right are evaluated from left to right, and only then is anything assigned, so a swap needs no third name.

```
fn main() ! console, alloc {
  var a = 1
  var b = 2
  (a, b) = (b, a)
  print("{a} {b}")
}
```

This prints `2 1`. The order of the elements is seen when they do something.

```
fn tick(name: str, n: i32) -> i32 ! console {
  print(name)
  n
}

fn main() ! console, alloc {
  (a, b) = (tick("first", 1), tick("second", 2))
  print("{a} {b}")
}
```

This prints `first`, `second`, and `1 2`.

## The empty tuple

The empty tuple `()` has no parts. It is what a function with no return type returns, and what an [`if`](if.md) with no `else` gives. A function that says `-> ()` and one that says no return type are the same function.

```
fn greet() ! console { print("hi") }

fn main() ! console, alloc {
  x = greet()
  y: () = x
}
```

There is nothing in it to take apart, so it is never the pattern of a binding: `() = x` is refused with "expected a pattern, found `()`".

## The forms

| Written | Is |
| --- | --- |
| `(a, b)` | A tuple of two values |
| `(T, U)` | The type of a tuple of a `T` and a `U` |
| `(x, y) = e` | Defines `x` and `y` as the two parts of `e` |
| `(x, var y) = e` | The same, where `y` is a `var` |
| `(x, y) = e`, where `x` is a `var` | Assigns `x` and defines `y` |
| `()` | The empty tuple, which a function with no return type returns |

Next: [Enums](enums.md)
