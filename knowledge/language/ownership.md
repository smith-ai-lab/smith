---
type: specification
title: Ownership
description: The Copy and Discard traits, which say whether a value may be duplicated and whether it may be dropped unused, and the three kinds of value they make.
status: planned
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
| Used at most once | `Discard` only | A random `Key` |
| Used exactly once | Neither | A raw file handle, a transaction |

A value that is used at most once may be dropped unused and may not be used twice. [`take`](take.md) is how that is checked. A value that is used exactly once may be neither, and is checked as [Values used exactly once](resources.md) says. A type gets the third kind by taking its traits away, as [`without`](without.md) says.

## Discard

A value that is never used is dropped, and that is an error when its type lacks `Discard`.

```
fn forget(path: str) -> Result[(), FsError] ! fs {
  f = try open_file(path)
  .ok(())
}
```

This is refused: `f` is a `File`, which cannot be dropped, and nothing closes it. A type with `Discard` is dropped without a word.

## Copy in a generic function

A generic function that duplicates a value says so in the bound of its type parameter.

```
fn pair[T: Copy](x: T) -> (T, T) { (x, x) }
```

Without `T: Copy`, `(x, x)` is refused, as `T` may be a type that cannot be copied. A bound is written where the function copies, and tooling proposes it.

## The forms

| Written | Is |
| --- | --- |
| `T: Copy` | A type parameter that may be duplicated |
| `T: Discard` | A type parameter that may be dropped unused |

Next: [without](without.md)
