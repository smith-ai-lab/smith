---
name: smith-dev-syntax-loops
description: "New syntax (#1428): `for x in xs:`, `while`, `while let`, `std.seq` (`range`, `enumerate`, `zip`, `refs`, `take`, `drop`, `peekable`), generators with `yield` returning `iter(T)` (no user-written `next`), containers looped through their `items`, `mut` on a loop name, `for` as an expression that builds an array, `continue` as a filter, fused reductions like `sum(for x in xs: x * x)`. Load when writing a loop or an iterator in the new syntax."
---

# Loops and iterators

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

```smith
from std.seq import range, enumerate, zip, refs

for x in xs: use(x)
for (i, x) in enumerate(xs): print_row(i, x)
for (a, b) in zip(xs, ys): add(a, b)            # variadic: zip(xs, ys, zs) gives triples
for i in range(n, 0, -1): step(i)
for p in refs(points): p.x += 1                 # refs gives *mut T to write through
for (i, mut x) in enumerate(copies): x *= 2     # mut on one name, as in let
while let .some(n) = todo.pop(): visit(n)       # until the pattern fails to match
```

- `for` loops over arrays, slices (so `str`), contiguous blocks (one row per turn), **iterators** (`iter(T)` values, made by generators) and **containers**: a value whose type's module has `items`, which `for x in c` calls as `c.items()`, so each loop gets a fresh pass. `std.seq` (`range`, `enumerate`, `zip`, `refs`, `take`, `drop`, `peekable`) is written with `yield`. There is no `map` or `filter`: a `for` expression does that.

## Generators

```smith
fn nats() -> iter(i32):
    let mut n = 0
    while true:
        yield n
        n += 1
```

- A function returning `iter(T)` with a `yield` in its body is a generator: lazy, nothing runs until the first `next`. `return` ends it; `return v` is an error ("`yield v` first"). `yield` stands only in the generator's own body, not in a lambda or a called function.
- It compiles to a state machine (its locals become a struct that resumes at the last `yield`), so it is zero cost when inlined. `iter(T)` is a concept; each generator has its own type. `it.next()` (gives `?T`) and `it.close()` (runs its `defer`s) are built in; nobody writes a `next`. A container's module defines its passes as generators:

```smith
pub fn items(l: *List(T)) -> iter(T):            # for x in shapes: ...
    for i in range(0, l.len): yield l.data[i]
pub fn refs(l: *mut List(T)) -> iter(*mut T):    # for p in refs(shapes): p.x += 1
    for i in range(0, l.len): yield &l.data[i]
```

## for as an expression

```smith
let doubled = for x in xs: x * 2                # the body's value: the loop builds an array
let evens = for x in xs:
    if x % 2 != 0: continue                     # continue skips an element: a filter
    x
let grid = for i in range(0, n):                # nested fors build a contiguous block [n, m]T
    for j in range(0, m): cell(i, j)
let energy = sum(for x in xs: x * x)            # fused into one loop: no temporary array
```

Over a fixed array the result is a fixed array on the stack; over a slice or an iterator it is a slice in the current allocator. `break` stops early. A body with no value builds nothing.

## Not settled yet

`zip` over slices of different lengths (the lead recommends a checked failure); what looping over a `str` yields (the lead recommends bytes, with `utf8.chars(s)` for code points); boxing an iterator to store different ones in one variable.
