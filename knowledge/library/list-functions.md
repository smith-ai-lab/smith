---
type: specification
title: List functions
description: The functions of std.list that search and compare any list, such as starts_with, find, and contains, which a text uses as its bytes, and append.
status: partial
built:
  - "`append` of `std.list`, which gives a list the elements of another after its own, with one test for room and one copy, for any `T: Copy`, and takes a `str` as a `List[u8]`"
  - "not `contains`, `index_of`, `find`, `starts_with`, or `ends_with`"
---

# List functions

The module `std.list` holds the functions that ask a question of a [`List`](list.md) and the one that joins two. Each is written once, for a list of any `T`, and a [`str`](../language/text.md) is used as its bytes, so the same function answers for text.

```
use std.list (starts_with)

fn is_arrow(source: str) -> bool { starts_with(source, "->") }
```

`is_arrow("->x")` is `true`, and `is_arrow("-")` is `false`. A `str` is passed where a `List[u8]` is expected, with no call and no copy.

## Asking where an element is

`contains` says whether an element is in the list, and `index_of` says where the first one equal to it is. Both need `T` to have [`Eq`](../language/comparisons.md), to tell one element from another.

```
fn contains[T: Eq](xs: List[T], x: T) -> bool
fn index_of[T: Eq](xs: List[T], x: T) -> Option[Nat]
```

```
use std.list (contains, index_of)

fn demo() {
  contains([3, 5, 8], 5)
  index_of([3, 5, 8, 5], 5)
  index_of("a=b", '=')
}
```

`contains([3, 5, 8], 5)` is `true`. `index_of([3, 5, 8, 5], 5)` is `.some(1)`, the place of the first one. `index_of("a=b", '=')` is `.some(1)`, a place in bytes. An element that is not there gives `false` and `.none`, and the empty list holds nothing, so `contains([], 5)` is `false` and `index_of([], 5)` is `.none`.

## Asking where a part is

`find` gives the place where a part first begins, and `starts_with` and `ends_with` ask whether a part is at the front or at the back. The part is a list of the same type.

```
fn find[T: Eq](xs: List[T], part: List[T]) -> Option[Nat]
fn starts_with[T: Eq](xs: List[T], prefix: List[T]) -> bool
fn ends_with[T: Eq](xs: List[T], suffix: List[T]) -> bool
```

```
use std.list (ends_with, find, starts_with)

fn demo() {
  find("let x = 1", "x =")
  starts_with([1, 2, 3], [1, 2])
  ends_with("main.smith", ".smith")
}
```

`find("let x = 1", "x =")` is `.some(4)`. `starts_with([1, 2, 3], [1, 2])` is `true`, and `ends_with("main.smith", ".smith")` is `true`.

A part is found where it first begins, so `find("abab", "ab")` is `.some(0)`. A part that is not there gives `.none`.

## What they give at the edges

- An empty `part` is found at 0, in any list, even an empty one: `find([1, 2], [])` is `.some(0)` and `find("", "")` is `.some(0)`.
- An empty `prefix` or `suffix` is one every list has: `starts_with(xs, [])` and `ends_with(xs, [])` are `true`, for an empty `xs` too.
- A part longer than the list is not in it. `find([1], [1, 2])` is `.none`, and `starts_with([1], [1, 2])` and `ends_with([1], [1, 2])` are `false`.
- A list that is empty holds no element, so `contains` is `false` and `index_of` is `.none` of it.

## Joining two lists

`append` gives a list of the elements of `a` and then those of `b`. It needs no `Eq`, as it compares nothing, and it allocates, as [`push`](list.md) does.

```
fn append[T: Copy](a: take List[T], b: List[T]) -> List[T] ! alloc
```

```
use std.list (append)

fn demo() ! alloc {
  append([1, 2], [3])
  append([], [3])
}
```

`append([1, 2], [3])` is `[1, 2, 3]`. `append([], [3])` is `[3]`, and with an empty `b` the result has the elements of `a` alone.

`append` is built as `push` is. It takes `a` as `push` takes its list, so `out = append(out, more)` grows `out` in place, as [a list is never copied unasked](../language/ownership.md#a-list-is-never-copied-unasked). It tests once whether `a`'s memory has room for the elements of `b`, and when it has not, asks once for memory that holds both and copies `a`'s elements, then it copies the elements of `b` after them in one go. `b` is lent, and its elements are copied, so `T` has `Copy`: a list of lists is joined with `push`.

## The forms

| Written | Gives |
| --- | --- |
| `contains(xs, x)` | `true` when an element of `xs` equals `x` |
| `index_of(xs, x)` | `.some` of the place of the first element equal to `x`, or `.none` |
| `find(xs, part)` | `.some` of the place where `part` first begins, or `.none`; an empty `part` is at 0 |
| `starts_with(xs, prefix)` | `true` when `xs` begins with `prefix`; an empty `prefix` is always one |
| `ends_with(xs, suffix)` | `true` when `xs` ends with `suffix`; an empty `suffix` is always one |
| `append(a, b)` | The elements of `a` and then those of `b`, with `alloc` |
