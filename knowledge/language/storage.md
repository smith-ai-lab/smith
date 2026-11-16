---
type: specification
title: Storage and arenas
description: When an array gets a buffer, the alloc effect and arenas, recursion without stack overflow, and types that contain themselves.
status: planned
---

# Storage and arenas

Array code never asks for memory. The compiler decides which arrays need a buffer, by a fixed rule about how each one is read.

- **An array fuses into its consumer** when the consumer reads each element once. No buffer exists.
- **An array is stored** when its elements are read repeatedly, or when more than one consumer uses it.

```
fn squares_plus_five[n: Nat](xs: [n]f32) -> f32 {
  ys = for i { xs[i] * xs[i] }
  zs = for i { ys[i] + 5.0 }
  sum(zs)
}
```

This compiles to a single loop with no buffers, because `zs` reads each `ys[i]` once and `sum` reads each `zs[i]` once.

The MLP shows the other case. Every output of the second layer reads every hidden value, so `hidden` is stored, and the `relu` fuses into the loop that fills it.

```
struct Mlp[i: Nat, h: Nat, o: Nat] {
  l1: Linear[i, h]
  l2: Linear[h, o]
}

fn forward[i: Nat, h: Nat, o: Nat](m: Mlp[i, h, o], x: [i]f32) -> [o]f32 {
  hidden = map(relu, apply(m.l1, x))
  apply(m.l2, hidden)
}
```

Data whose size is known only at run time, such as a growing list or a map, allocates through the `alloc` effect. A function's temporaries are released when it returns, and its result is built directly in its caller's memory. `with arena()` handles `alloc` for the rest of a block with a fresh arena, which is freed whole when the block ends.

```
fn squares(n: Nat) -> List[f32] ! alloc {
  var out: List[f32] = []
  loop i in 0..n { out = push(out, float(i) * float(i)) }
  out
}

fn sum_of_squares(n: Nat) -> f32 ! alloc {
  sum(squares(n))
}

fn handle(req: Request) -> Response {
  with arena()
  respond(req, sum_of_squares(req.n))
}
```

- **Every dynamic value owns its storage.** There are no references, so no pointer can cross from one arena into another.
- **Every value's arena is known at compile time.** Nothing stores an arena pointer.
- **Results are built in place** in the caller's memory, so nothing is copied on return.
- A graph, or anything else with shared or cyclic links, is a collection plus indices into it, because a value cannot point back at itself.
- **A `var` that is replaced is updated in place** when its old value is provably unused.

Recursion cannot overflow the stack.

```
// A tail call: compiled as a loop, with no frame per call.
fn power(x: f64, n: Nat, acc: f64) -> f64 {
  if n == 0 { acc } else { power(x, n - 1, acc * x) }
}

// Not a tail call: one frame per level, kept in the arena.
fn power_slow(x: f64, n: Nat) -> f64 ! alloc {
  if n == 0 { 1.0 } else { x * power_slow(x, n - 1) }
}
```

- A tail call always compiles to a loop, so a tail-recursive function uses no stack and may run in a GPU kernel.
- Any other recursion keeps its frames in the current arena, so its depth is limited only by memory. Such a function carries `alloc`.
- Code with no recursion has a deepest chain of calls that is known at compile time, and the compiler confirms it fits the stack.

A type may contain itself, which is how a tree is written. The compiler stores such a field out of line, in the same arena as its parent, and no pointer appears in the source.

```
enum Expr {
  num: f64
  neg: Expr
  add: Pair
}

struct Pair {
  left: Expr
  right: Expr
}

fn eval(e: Expr) -> f64 ! alloc {
  match e {
    .num(v) { v }
    .neg(x) { -eval(x) }
    .add(p) { eval(p.left) + eval(p.right) }
  }
}
```

- A tree is a value. Values are immutable, so two trees may share a subtree and nothing can observe it, and a tree can never contain a cycle.
- Nodes are bumped into the arena and dropped with it, so there is no per-node allocation, reference count, or collector.
- No address is observable, so the compiler picks the layout, such as 32-bit offsets within the arena.
- Changing a tree builds new nodes and leaves the old ones until the arena is dropped. A pass that rewrites a tree many times runs in its own arena and builds its result in the parent's.

Next: [Differentiation](differentiation.md)
