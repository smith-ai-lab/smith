---
type: specification
title: without
description: A without statement takes away a trait a type would get automatically, for Copy, Discard, Fmt, Repr, Eq, Ord, Random, or any other that is derived.
status: partial
built: "`without`, for `Fmt`, `Repr`, and `Eq`"
---

# without

A type opts out of a trait it would get automatically with a `without` statement.

```
struct Cat {
  name: str
}

without Fmt for Cat
```

`Cat` would be given `Fmt`, as [Repr](repr.md) says, and now it has none. `"{c}"` for a `Cat` named `c` is refused: "there is no implementation of `Fmt` for `Cat`". `repr(c)` still prints `Cat(name: "tom")`, as `Cat` keeps `Repr`.

## Taking away what a type is given

`without` works for any trait that is derived automatically. The traits so far are `Copy` and `Discard` from [Ownership](ownership.md), `Fmt` and `Repr`, `Eq` and `Ord` from [Traits](traits.md), and `Random` from [Tests](tests.md).

```
struct Cat {
  name: str
}

without Eq for Cat

fn main() ! console, alloc {
  a = Cat(name: "tom")
  b = Cat(name: "tom")
  print("{a == b}")
}
```

This is refused: "there is no implementation of `Eq` for `Cat`".

`without Repr` takes `Fmt` too, as the `Fmt` a type is given is its `Repr`. Then `"{c}"` is refused with "there is no implementation of `Fmt` for `Cat`, as `Cat` has no `Repr`".

## What a field does

A trait taken away from a type is taken away from every type that holds it, because a type has a derived trait only when each of its fields does.

```
struct Home {
  cat: Cat
}
```

With `without Fmt for Cat` above, `"{h}"` for a `Home` named `h` is refused: "there is no implementation of `Fmt` for `Home`, as `Cat` has no `Fmt`".

The fields of the type that opts out stay as they are. A `Transaction` below has a field `id` that has `Copy` and `Discard`, and it is only the `Transaction` that has neither.

```
struct Transaction {
  id: u64
}

without Copy for Transaction
without Discard for Transaction
```

A `Transaction` is a value used exactly once, as [Values used exactly once](resources.md) shows.

## Two ways to change a trait

Where a type needs another meaning for a trait it is given, it writes its own `impl`, which replaces the one it was given, as [Traits](traits.md) says. `without` is for a type that must have no such trait at all.

## The forms

| Written | Is |
| --- | --- |
| `without Fmt for T` | `T` cannot be written in a string |
| `without Repr for T` | `T` has no `Repr`, and no `Fmt` |
| `without Eq for T` | `T` cannot be compared with `==` |
| `without Copy for T` | `T` cannot be duplicated |
| `without Discard for T` | `T` cannot be dropped unused |

Next: [Lending and take](take.md)
