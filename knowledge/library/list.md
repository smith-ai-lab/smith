---
type: specification
title: List
description: List, an array kept together with its size, which can grow.
---

# List

A `List[T]` is an [array](../language/arrays.md) kept together with its size, for a sequence whose length is known only when the program runs. It is an ordinary struct, in which the type of one field mentions the field before it.

```
struct List[T] {
  size: Nat
  items: [size]T
}
```

- `items` is an array like any other, so an index into it is a `Fin[size]` that needs no check, and it can be sliced, looped over, and summed.
- A list implements [`Index`](../language/arrays.md), so `list[i]` reads the element `list.items[i]`.
- `[]` is the empty list, and a literal such as `[1, 2, 3]` becomes a list where one is expected.
- `push` gives a list with one more element. When the list it is given is a `var` that is assigned the result, as in `out = push(out, x)`, the list grows in place.

```
fn push[T](list: List[T], item: T) -> List[T] ! alloc
```
