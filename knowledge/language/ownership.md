---
type: specification
title: Ownership
description: The Copy and Discard traits, which say whether a value may be duplicated and whether it may be dropped unused, and the three kinds of value they make.
status: partial
built:
  - "`Copy` and `Discard` given to a type exactly when each of its fields has them"
  - "A value without `Copy` used again after it was given away on some path, which includes a later round of a loop, refused at the second use"
  - "a value without `Copy` that a lambda gives away is spent in the function it is written in from the call the lambda is passed to, when that call takes its function"
  - "a type parameter has `Copy` and `Discard` only where a bound or a `requires` line asks, so a value of one is used at most once and must be taken, and a call, or an implementation it chooses, is given only a type that has what the bounds ask"
---

# Ownership

Two traits say what may happen to a value. `Copy` means it may be duplicated, and `Discard` means it may be dropped unused.

```
struct Point {
  x: u64
  y: u64
}

fn mirror(p: Point) -> Point {
  q = p
  Point(x: q.y, y: p.x)
}
```

`p` is used after `q = p` copied it, which is allowed because `Point` has `Copy`. A number has `Copy`, so a struct of numbers has it.

## Derived

Both traits are derived automatically: a type has each trait unless one of its fields lacks it. Nothing is written for a type to have them.

```
struct Seeded {
  key: Key
  count: u64
}
```

A `Key`, the type [random numbers](take.md) come from, has `Discard` and not `Copy`. So `Seeded` has `Discard` and not `Copy`, because one of its fields lacks it, and `count` having both changes nothing.

## The three kinds

The two traits make three kinds of value.

| Kind | Traits | Examples |
| --- | --- | --- |
| Ordinary | `Copy` and `Discard` | Numbers, arrays, most structs |
| Used at most once | `Discard` only | A random `Key`, a `List` |
| Used exactly once | Neither | A raw file handle, a transaction |

A value that is used at most once may be dropped unused and may not be used twice. [`take`](take.md) is how that is checked. A value that is used exactly once may be neither, and is checked as [Values used exactly once](resources.md) says. A type gets the third kind by taking its traits away, as [`without`](without.md) says.

## A list is never copied unasked

A [`List`](../library/list.md) has `Discard` and not `Copy`, as the library takes its `Copy` away with `without Copy for List`. Copying a list costs time in proportion to its length, and nothing in Smith costs that without being written.

```
fn both(xs: take List[u8]) -> (List[u8], List[u8]) ! alloc {
  ys = push(copy(xs), 1)
  (xs, ys)
}
```

`push` [takes](take.md) its list, so the list it gives back is the same memory with one more element, always, and not only where the compiler can see that it may be. `both` wants the old list too, so it writes `copy(xs)`. Without the `copy`, `xs` is used after it was given to `push`, and that is refused.

Reading a list lends it and copies nothing: its `size`, an element, and passing it to a function that does not take it. A struct that holds a list has no `Copy` either, by the rule above, so the state of a program that is kept in a struct is moved and never duplicated. A `str` has `Copy`: a text cannot be changed, so two names for one text share it. So has a [`Slice`](../library/slice.md), which is what a list becomes when a program is done building it.

The compiler does not build this yet. Today a list that is used after it is given away is copied.

## Discard

A value that is never used is dropped, and that is an error when its type lacks `Discard`.

```
fn forget(path: str) -> Result[(), FsError] ! fs {
  f = try open_file(path)
  .ok(())
}
```

This is refused: `f` is a `File`, which cannot be dropped, and nothing closes it. A type with `Discard` is dropped without a word.

## Copy and Discard in a generic function

Nothing is known of a type parameter but what its bounds say, so `T` may be a type with neither trait. A value of type `T` is used at most once and must be taken on every path: given to a function that takes it, returned, or stored. Looking at it, lending it, and reading a field of it take nothing. A function that duplicates one says so in a bound, and so does one that drops one.

```
fn keep[T](a: take T) -> T { a }

fn pair[T: Copy](x: T) -> (T, T) { (x, x) }

fn first[T: Discard](a: take T, b: take T) -> T { a }
```

Without `T: Copy`, `(x, x)` is refused at the second `x`, as `T` may be a type that cannot be copied. Without `T: Discard`, `first` is refused at `b`, which nothing takes, and so is a function that only lends `a` and ends. Each error says which bound to write, and tooling proposes it. `requires Copy[T]` and `requires Discard[T]` say the same as the bounds, as [`requires`](bounds.md) says of any trait.

A function that only reads what it is lent needs neither, as `fn size_of[T](xs: List[T]) -> Nat { xs.size }` shows, and neither does one that gives a value on. A value that holds a type parameter, as a struct, a tuple, an enum, or a list does, has each trait exactly when what it holds has it.

A call gives `T` only a type that has what the bounds ask. `pair(Key(secret: 1))` is refused at the call: the `Key` has no `Copy`, which `pair` needs of `T`. The same holds for an `impl` with a bound on its type parameters, where it is chosen.

## The forms

| Written | Is |
| --- | --- |
| `T: Copy` | A type parameter that may be duplicated |
| `T: Discard` | A type parameter that may be dropped unused |

Next: [without](without.md)
