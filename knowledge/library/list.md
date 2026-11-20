---
type: specification
title: List
description: List, an array kept together with its size, which can grow.
status: partial
built:
  - "a `List` of any type a program can make, written `List[u8]` or `List[Person]`, built from `[]` or a literal such as `[1, 2, 3]` where a list of a known type is expected, grown with `push`, which takes its list and writes in place while the memory has room, wherever the list is held, in a `var`, in a field, or as what a function takes and returns, and read with `size`, a `Nat`, and an index, a `Nat` that is proven in range, of a `var` as of any name, where `xs[i].field` reads only that field"
  - "a list may be held inside a struct, a tuple, an enum, or another list, and be a function's parameter and result, and a list is held by one name, so no name sees another's `push`, and `copy` of `std.list` gives a new list of the same elements"
  - "a function with a type parameter, bounded by `Eq` or `Ord` or by nothing, takes and gives a `List[T]`, reads its size and elements, compares them, loops over them, and pushes, and is specialised for each element type as any generic function is; a list written out where `T` is settled by no other argument is refused with one error, as is `[]`, and one where another argument settles it takes that type"
  - "`loop v in xs` over a `List` of any type, which is built as [`loop`](../language/loop.md) says"
  - "`==` of two lists of one type, and `Fmt` and `Repr` of a list, each as its element has them, alone and inside a struct, a tuple, an `Option`, or another list; the same for a `Slice`"
  - "not `items`, slicing, summing, or a pattern in brackets"
---

# List

A `List[T]` is an [array](../language/arrays.md) kept together with its size, for a sequence whose length is known only when the program runs. It is an ordinary struct, in which the type of one field mentions the field before it.

```
struct List[T] {
  size: Nat
  items: [size]T
}
```

- `items` is an array like any other, so an index proven to be below `size` needs no check, and it can be sliced, looped over, and summed.
- A list implements [`Index`](../language/indexing.md), so `list[i]` reads the element `list.items[i]`, and it compiles where `i < list.size` is proven:

  ```
  impl[T] Index[List[T], Nat, T] {
    fn valid(c: List[T], i: Nat) -> bool { i < c.size }
    fn at(c: List[T], i: Nat) -> T { c.items[i] }
  }

  fn total(list: List[f32]) -> f32 {
    var sum = 0.0
    loop i in 0..list.size { sum = sum + list[i] }
    sum
  }
  ```
- `[]` is the empty list, and a literal such as `[1, 2, 3]` becomes a list where one is expected.
- A pattern in brackets matches a list of exactly that many elements, as [Patterns in brackets](../language/array-patterns.md) says, which is how a program reads its arguments:

  ```
  fn main(args: List[str]) -> Result[(), CliError] ! console, fs, alloc {
    match args {
      [] { usage() }
      ["build", path] { build(path) }
      ["run", path] { run(path) }
      other { unknown(other) }
    }
  }
  ```
- `push` takes a list and gives it back with one more element. When the list has room it is written in place, wherever it is held, and otherwise its elements are copied into memory with room for twice as many, or for one when there are none. The memory of a list holds its elements and nothing else.

```
fn push[T](list: take List[T], item: take T) -> List[T] ! alloc
```

A list is what a program builds, and [`freeze`](slice.md), a function of `std.list` as `push` and `copy` are, turns one that is done into a `Slice`, which is shared freely. A list is never copied unless `copy` is written: `push` takes the list it is given, and a list used after it is given away is refused, as [Ownership](../language/ownership.md#a-list-is-never-copied-unasked) says.

```
fn copy[T: Copy](list: List[T]) -> List[T] ! alloc
```

`copy(xs)` gives a new list of the same elements, which `xs` lends and `push` to either does not change in the other. It needs `T: Copy`, so a list of lists is copied by copying each list in it.

The functions that search a list, compare its ends, and join two lists are in [List functions](list-functions.md).
