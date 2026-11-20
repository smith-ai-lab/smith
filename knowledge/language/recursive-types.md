---
type: specification
title: Types that contain themselves
description: A type may contain itself, which is how a tree is written, and a graph is a collection plus indices.
status: partial
built:
  - "a struct or an enum that holds itself only through a `List`, directly or through other types, as `struct Node { kids: List[Node] }`, which is accepted where it is declared, and a value of it is built"
  - "a type that holds itself with no `List` between, directly or through others, refused with one error, at the name that closes the cycle, that says it is not built yet and that a `List` of it is"
  - "`==`, `Repr`, `Fmt`, and `Hash` of a type that holds itself through a `List`, refused at the use with one error, until a function may call itself"
  - "not the page's own example, which holds itself with no `List`, nor recursion, nor a tree walked to any depth"
---

# Types that contain themselves

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

`Expr` holds an `Expr` in `neg`, and holds one through `Pair` in `add`. `eval` recurses over it, so it carries `alloc` as [Recursion](recursion.md) says.

## Where a type may hold itself

A type holds itself only where it is held: as a field, as what a case holds, or as an element of a list or an array inside it. It may not hold itself as a parameter of a function it holds, so `enum T { wrap: fn(T) -> T }` is refused. [What is trusted](trust.md) says why: such a type lets a computation go on for ever with no recursion to be seen.

## A tree is a value

Values are immutable, so two trees may share a subtree and nothing can observe it. A tree can never contain a cycle.

```
fn twice(x: Expr) -> Expr { .add(Pair(left: x, right: x)) }
```

`twice` holds the same `x` on both sides, and `eval` of the result is twice `eval(x)`.

## Where the nodes live

Nodes are bumped into the [arena](arenas.md) and dropped with it, so there is no per-node allocation, reference count, or collector. No address is observable, so the compiler picks the layout, such as 32-bit offsets within the arena.

## Changing a tree

Changing a tree builds new nodes and leaves the old ones until the arena is dropped.

```
fn negate_all(e: Expr) -> Expr ! alloc {
  match e {
    .num(v) { .num(-v) }
    .neg(x) { x }
    .add(p) { .add(Pair(left: negate_all(p.left), right: negate_all(p.right))) }
  }
}
```

A pass that rewrites a tree many times runs in its own arena and builds its result in the parent's.

```
fn rewritten(e: Expr) -> Expr ! alloc {
  with arena()
  negate_all(negate_all(e))
}
```

## A graph

A value cannot point back at itself, so a graph, or anything else with shared or cyclic links, is a collection plus indices into it.

```
struct Graph {
  labels: List[str]
  next: List[Nat]
}
```

`next[i]` is the index in `labels` of what `labels[i]` links to.

## The forms

| Shape | Written as |
| --- | --- |
| A tree | A type that holds itself, directly or through another |
| Shared or cyclic links | A collection and indices into it |

Next: [Functions over code](code.md)
