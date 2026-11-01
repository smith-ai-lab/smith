---
name: smith-prove-slices
description: "smith prove on slices and arrays: a parameter `i32 xs[4]` is 4 objects, `T xs[:]` is countof(xs) objects, a loop over a slice proved with contract::invariant, an index that may be out of range, p[a:b] views, slices of rows. Load when proving a function that indexes a slice or an array."
---

# Proving slices and arrays

A slice carries its count, so an index is proved against it with no contract. The loop needs an invariant:

```smith
module stats;

import std::contract;

export i32 largest(i32 xs[:]) {
    contract::requires(countof(xs) > 0);
    i32 best = xs[0];
    for (usize i = 1; i < countof(xs); i++) {
        contract::invariant(1 <= i && i <= countof(xs));    // proved by induction: one pass, whatever the count
        best = xs[i] > best ? xs[i] : best;
    }
    return best;
}
```

In a project, `smith prove -v` says `ok  stats::largest  (stats.smith:5)  0.03 s wall, 0.03 s CPU` and `a loop proved by induction`; the file form `smith prove stats.smith` cannot name what `smith_failure` writes and says the loop has no bound, so prove a project.

- **An array parameter of a module is N objects** (`i32 xs[4]`, see `smith-lang-arrays`): `xs[i]` is proved in range by `contract::requires(i < 4)` alone. A C parameter `T xs[static N]` says the same; a plain `T xs[N]` in C is a pointer to one.
- **A slice parameter `T xs[:]` of a module is `countof(xs)` objects** (`smith-lang-arrays`): `if (i < countof(xs)) return xs[i];` is proved with no `contract::requires`, and a loop up to `countof(xs)` is proved with a `contract::invariant(1 <= i && i <= countof(xs))` in its body (`largest` above: 0.03 s of CPU), or unrolled under `contract::requires(countof(xs) <= 8)`; a large bound unrolls past the 10 s budget. An index or a slice (`xs[i]`, `xs[a:b]`) that may be out of range fails as `assert(an index or a slice is in range) may fail` at its line, in the proved function and in what it inlines. A view of a pointer, `p[a:b]`, is held to what `p` is known to point at when its elements are read: `T *p` is one object, so `p[0:2]` read at `[1]` fails, and `T p[4]` (or `T p[static 4]`) makes `p[0:4]` fine. Not yet proved at the slice itself (the lowering leaves no marker): a `p[a:b]` never read. **A slice of rows** (`f32 m[:][2]`, `m[:][:]`, `t[:][3][:]`) points at its rows the same way: `if (r < countof(m) && c < countof(m[r])) return m[r][c];` is proved with no contract (about 2 s), an inner index past its own count fails at its line, and `restrict` on a slice of rows (`u8 dst[restrict :][:]`) is checked at the call like a `[:]`. Each dimension and the whole are assumed under 2^20 elements; a `smith_requires` may not index a slice of rows (`countof(m[0])`: use `countof(m)`), and a read through a row view (`f32 row[:] = m[r]`) takes about 8 s of CPU. The count is assumed at most 2^32; a slice that arrives in memory (a field, a pointer to one) is not assumed to hold anything.

See `smith-prove`, `smith-lang-arrays`, `smith-lang-array-params`.
