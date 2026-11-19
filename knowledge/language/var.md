---
type: specification
title: Mutable locals
description: Locals that change, declared with var, given a new value with =, and accumulated into with +=, -=, *=, and /=.
status: partial
built:
  - "`var` and `=`, an error for assigning to a name that is not a `var`"
---

# Mutable locals

`var` defines a local that can change. `var x = e` is a [binding](bindings.md) that may be given a new value later.

```
fn main() ! console {
  var word = "first"
  print(word)
  word = "second"
  print(word)
}
```

This prints `first`, then `second`. `=` with no `var` before it gives the name its new value.

So `x = e` does one of two things. It defines `x` when the function has no such name, and it assigns to `x` when `x` is a `var`. Only a `var` may be assigned to, as [Bindings](bindings.md) says.

## Stating the type

A `var` may state its type, as a binding does: `var word: str = "first"`. It always has a first value. `var word: str` alone is refused: "expected `=`, found a line break".

## Giving it a new value of its type

A `var` keeps the type of its first value, so a new value of another type is refused. The range of a whole number is part of its type, as the next section says.

```
fn main() ! console {
  var word = "a"
  word = 5
  print(word)
}
```

This gives "this is a number, where text is needed". A stated type that the first value does not have is refused the same way: `var n: i32 = "a"` gives "this gives text, where a `i32` is needed".

A name is never defined where it can already be used, `var` or not. `var word = "b"` after `word` is defined is refused: "`word` is already defined in `main`".

## The range of a `var`

A `var` with no stated type has the range of what it is first given. A value outside that range is refused, with the fix to state the range of the `var`.

```
fn main() ! console, alloc {
  var level: 0..101 = 0
  level = 100
  print("{level}")
}
```

This prints `100`. Without the stated range, `var level = 0` is a `0..1`, and `level = 100` is refused, as `100` is not in it. So a `var` that a [`loop`](loop.md#a-var-the-loop-changes) adds to states its range, and each assignment in the loop is proven to stay in it.

## Accumulating

`+=`, `-=`, `*=`, and `/=` change a `var` by a value. `total += 1.0` adds `1.0` to `total`, and `-=`, `*=`, and `/=` subtract, multiply, and divide.

```
fn grow(start: f32, rate: f32) -> f32 {
  var total = start
  total = total * (1.0 + rate)
  total += 1.0
  total
}
```

`grow(10.0, 0.5)` is `16.0`. With a whole number, `n += 1` is `n = n + 1`, and the result enters the range of `n`, as [Integer arithmetic](arithmetic.md#where-a-value-enters-a-narrower-range) says.

```
fn main() ! console, alloc {
  var n: 0..11 = 0
  n += 1
  print("{n}")
}
```

This prints `1`. `n` was last given `0`, so `n + 1` is `1`, which the range holds. The same four forms assign to a field of a `var`, which [Structs](structs.md) shows.

## Where a `var` can be used

A [lambda](lambdas.md) written in the function may assign its `var`s. A `var` is a name like any other, so it can be used only after the line that defines it, and a `var` defined inside a block, such as that of an [`if`](if.md), can be used only inside that block.

## The forms

| Written | Is |
| --- | --- |
| `var x = e` | A local that starts as `e`, and may change |
| `var x: T = e` | The same, with its type stated |
| `x = e` | Gives the `var` `x` a new value |
| `x += e`, `x -= e`, `x *= e`, `x /= e` | Adds `e` to `x`, subtracts it, multiplies by it, or divides by it |

Next: [`if`](if.md)
