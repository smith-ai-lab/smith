---
name: smith-dev-syntax-loops
description: "New syntax (#1428): `for x in xs`, `while`, `range`/`enumerate`/`zip`/`refs` streams, what can be looped over, `for` as an expression that builds an array, `continue` as a filter, nested `for` building a block, fused reductions like `sum(for ...)`. Load when writing a loop in the new syntax."
---

# Loops

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Loops

```smith
for x in xs { use(x) }
for (i, x) in enumerate(xs) { print_row(i, x) }
for (a, b) in zip(xs, ys) { add(a, b) }        // variadic: zip(xs, ys, zs) gives triples
for i in range(0, n) { step(i) }
for i in range(n, 0, -1) { step(i) }
for p in refs(points) { p.x += 1 }             // refs yields *mut T to write through
while queue.len() > 0 { work(queue.pop()) }
```

- The language loops only over built-in types: fixed arrays, slices (so `str`), contiguous blocks (one row per turn) and streams. Anything else is a function that returns a stream: `range`, `enumerate`, `zip`, `refs`, and user iterators.
- C's three-part `for` and today's `for (Usize i, T x : xs)` are errors; the fix is `range`, `enumerate` or `while`.
- `for` stays a keyword; the std library has no `map` or `each` over arrays.

## for as an expression

```smith
doubled := for x in xs { x * 2 }               // the body's value: the loop builds an array

evens := for x in xs {
    if x % 2 != 0 { continue }                 // continue skips an element: a filter
    x
}

grid := for i in range(0, n) {                 // nested fors build a contiguous block [n,m]T
    for j in range(0, m) { cell(i, j) }
}

energy := sum(for x in xs { x * x })           // fused into one loop: no temporary array
```

Over a fixed array the result is a fixed array on the stack; over a slice, range or stream it is a slice in the current allocator. `break` stops early. A body with no value builds nothing.

## Not settled yet

`zip` over slices of different lengths (the lead recommends a checked failure); what looping over a `str` yields (the lead recommends bytes, with `utf8.chars(s)` for code points).
