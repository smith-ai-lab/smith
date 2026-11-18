---
type: specification
title: List
description: List, an array kept together with its size, which can grow.
status: partial
built: "a `List` of any type a program can make, written `List[u8]` or `List[Person]`, built from `[]` or a literal such as `[1, 2, 3]` where a list of a known type is expected, grown with `push`, in place where a `var` is given the result of pushing to itself, and read with `size` and an index that is proven in range, of a `var` as of any name, where `xs[i].field` reads only that field; a list may be held inside a struct, a tuple, an enum, or another list, and be a function's parameter and result, and a name that holds a list never sees another name's `push`; not `items`, slicing, summing, a pattern in brackets, or looping over the elements"
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
- `push` gives a list with one more element. When the list it is given is a `var` that is assigned the result, as in `out = push(out, x)`, the list grows in place.

```
fn push[T](list: List[T], item: T) -> List[T] ! alloc
```
