---
type: specification
title: The alloc effect and arenas
description: Data whose size is known only at run time, such as a growing list or a map, allocates through the alloc effect, and with arena() gives the rest of a block a fresh arena that is freed whole when the block ends.
status: partial
built:
  - "the `alloc` effect, carried by every function that builds a text at run time, whose memory is kept until the program ends"
---

# The alloc effect and arenas

Data whose size is known only at run time, such as a growing list or a map, allocates through the `alloc` effect, and an arena is the memory it allocates from.

```
fn greet(name: str) -> str ! alloc { "hello {name}" }

fn main() ! console, alloc {
  print(greet("world"))
}
```

This prints `hello world`. `greet` builds a text whose length depends on `name`, so it carries `alloc`, and `main` lists it because it calls `greet`.

## What carries alloc

A function that builds a text with an expression in it, or grows a `List`, carries `alloc`.

```
fn squares(n: u64) -> List[u64] ! alloc {
  var acc: List[u64] = []
  loop i in 0..n { acc = push(acc, wrapping_mul(i, i)) }
  acc
}

fn main() ! console, alloc {
  print("{squares(5).size}")
}
```

This prints `5`. Leaving `alloc` out of `squares` is refused: "`push` needs the `alloc` effect, which `squares` does not declare", with the fix "write `fn squares(n: u64) -> List[u64] ! alloc`". A caller that lacks it is refused in the same way, at the call: for `fn count() -> u64 { squares(5).size }` it is "`squares` needs the `alloc` effect, which `count` does not declare", with the fix "write `fn count() -> u64 ! alloc`".

## What a function leaves behind

A function's temporaries are released when it returns, and its result is built directly in its caller's memory, so nothing is copied on return.

```
fn sum_of_squares(n: u64) -> u64 ! alloc {
  xs = squares(n)
  var total = 0
  loop i in 0..xs.size { total = wrapping_add(total, xs[i]) }
  total
}
```

`xs` is a temporary of `sum_of_squares`, and is gone when it returns the `u64`. The list `squares` returns is built where `sum_of_squares` keeps `xs`.

A `var` that is replaced is updated in place when its old value is provably unused, as `acc` is in `squares`: each `push` adds to the buffer it already has.

## with arena

`with arena()` handles `alloc` for the rest of a block with a fresh arena, which is freed whole when the block ends.

```
fn handle(req: Request) -> Response {
  with arena()
  respond(req, sum_of_squares(req.n))
}
```

`handle` carries no `alloc`, though `sum_of_squares` does, as the arena handles it. Everything `respond` and the functions below it allocate is freed when `handle` returns, and the `Response` is built in the caller's memory.

## Whose memory a value is in

- **Every dynamic value owns its storage.** There are no references, so no pointer can cross from one arena into another.
- **Every value's arena is known at compile time.** Nothing stores an arena pointer.

In `handle`, the list `sum_of_squares` builds belongs to the arena of the `with`, and the `Response`, which `handle` returns, is in the caller's.

## The forms

| Written | Is |
| --- | --- |
| `fn f() ! alloc` | `f` allocates, and its caller must have `alloc` or handle it |
| `with arena()` | The rest of the block allocates in a fresh arena, freed when the block ends |

Next: [Recursion](recursion.md)
