---
type: specification
title: Mutable locals
description: Locals that change, declared with var, given a new value with =, and accumulated into with +=, -=, *=, and /=.
status: partial
built: "`var` and `=`, an error for assigning to a name that is not a `var`"
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

A `var` keeps the type of its first value, so a new value of another type is refused.

```
fn main() ! console {
  var word = "a"
  word = 5
  print(word)
}
```

This gives "this is a number, where text is needed". A stated type that the first value does not have is refused the same way: `var n: i32 = "a"` gives "this gives text, where a `i32` is needed".

A name is defined once per function, `var` or not. `var word = "b"` after `word` is defined is refused: "`word` is already defined in `main`".

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

`grow(10.0, 0.5)` is `16.0`. The same four forms assign to a field of a `var`, which [Structs](structs.md) shows.

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
