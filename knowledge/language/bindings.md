---
type: specification
title: Bindings
description: How x = e defines an immutable name, which is never defined again while it can be used, how a binding states its type, and where a name can be used.
status: partial
built:
  - "`x = e` and `x: str = e`, refused where the name can already be used, and a name defined again once the block that defined it has ended"
---

# Bindings

A binding gives a name to a value. `x = e` defines the name `x` as the value of `e`, and `x` has that value from then on.

```
fn hypot(a: f32, b: f32) -> f32 {
  aa = a * a
  bb = b * b
  sqrt(aa + bb)
}
```

`aa` and `bb` are used on the line below the ones that define them, and `hypot(3.0, 4.0)` is `5.0`. A binding gives no value of its own, so it is never the last line of a block.

A name may be bound to another name, or to a call.

```
use std.console (console, print)

fn shout() -> str { "hey" }

fn main() ! console {
  word = shout()
  same = word
  print(same)
}
```

This prints `hey`.

## Stating the type

A binding may state its type, written as a parameter's is, with a colon after the name.

```
use std.console (console, print)

fn main() ! console {
  word: str = "typed"
  print(word)
}
```

This prints `typed`. Most bindings do not state their type, because the type of the value is already known, and `word = "typed"` means the same. A value of another type is refused.

```
use std.console (console, print)

fn main() ! console {
  word: str = 5
  print(word)
}
```

This gives "this is a number, where text is needed".

## A name never changes

A name is never defined where it can already be used, so there is no shadowing, and it keeps its value. Binding it again, or assigning to it, is refused.

```
use std.console (console, print)

fn main() ! console {
  word = "first"
  word = "second"
  print(word)
}
```

This gives "`word` is not a `var`, so it cannot be assigned to", with the fix "declare it with `var`: write `var word` where it is defined". A name that does change is a [`var`](var.md).

A parameter is a name too. Assigning to one gives the same message, with the fix "declare a `var` that starts as the parameter, as in `var new_name = name`".

```
fn shout(word: str) -> str {
  word = "x"
  word
}
```

## Where a name can be used

A name can be used only after the line that defines it.

```
use std.console (console, print)

fn main() ! console {
  print(word)
  word = "late"
}
```

This gives "there is no name `word` defined before this in `main`".

A name defined inside a block, such as the block of an [`if`](if.md), can be used only inside that block.

```
use std.console (console, print)

fn main() ! console {
  if true { inner = "here" }
  print(inner)
}
```

This gives "there is no name `inner` defined before this in `main`".

## A name after its block has ended

A name stops where its block ends, so the same name may be defined again after it. Two loops that stand one after the other may both count with `i`, and the arms of a [`match`](match.md) may each bind `v`.

```
use std.bits (u64)
use std.int (wrapping_add)

fn both(xs: List[u64], ys: List[u64]) -> u64 {
  var total: u64 = 0
  loop i in 0..xs.size { total = wrapping_add(total, xs[i]) }
  loop i in 0..ys.size { total = wrapping_add(total, ys[i]) }
  total
}
```

`both([1, 2], [3])` is `6`. Nothing is shadowed: the first `i` cannot be used where the second is defined. So at every line a name means one thing, and a name that can be used there is never defined again.

## What a body must end with

A body that ends with a binding has no value, so a function that states a type is refused.

```
fn broken(x: str) -> str {
  y = x
}
```

This gives "`broken` returns `str`, and its body ends with a binding or an assignment, which gives nothing". Ending with the name gives the value.

```
fn mended(x: str) -> str {
  y = x
  y
}
```

## The forms

| Written | Is |
| --- | --- |
| `x = e` | A name for the value of `e`, which never changes |
| `x: T = e` | The same, with its type stated |

Next: [Mutable locals](var.md)
