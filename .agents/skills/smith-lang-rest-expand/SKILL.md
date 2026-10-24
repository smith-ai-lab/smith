---
name: smith-lang-rest-expand
description: "Pack expansion in a generic: `pattern...` writes one copy of an expression per element of `auto... rest` (`f(leaf(rest, off)...)`, `{(f64)rest...}`), an empty pack and its comma, and the errors '`...` expands nothing', 'an expansion does not nest'. Load when a generic writes `...` after an expression that names the pack."
---

# Pack expansion: `pattern...`

`rest...` spreads a pack; `pattern...` writes one copy of any expression that names it, with `rest` replaced by each element. `...` after an expression is invalid C, so the C rule allows it.

```smith
module packs;

export f64 largest(auto first, auto... rest) {
    f64 values[1 + countof(rest)] = {(f64)first, (f64)rest...};   // one cast per argument
    f64 best = values[0];
    for (usize i = 1; i < 1 + countof(rest); i++) best = values[i] > best ? values[i] : best;
    return best;
}

export f64 demo() {
    return largest(4, 9.5, 2);                                    // 9.5
}
```

`f(leaf(rest, off)...)` is `f(leaf(rest_0, off), leaf(rest_1, off), ...)`; `rest...` is the pattern `rest` alone.

## Rules

- It stands as an argument of a call or of a braced initializer, among other arguments (`f(a, g(rest)..., b)`, `{g(rest)..., 0}`).
- The pattern may name `rest` several times, hold calls to generics and `x->[f]` (`rest->[f]...`); `rest[i]` and `countof(rest)` inside it are not the pack.
- The other arguments of the call (`p + 0`, a local) and a generic called in the pattern are typed with the instance's names (`inner(p + 0, same(rest, k)...)`).
- An empty `rest` writes nothing, and the comma before it (else the one after) goes with it: `f(a, g(rest)...)` is `f(a)`.

## Errors

- ``` `...` expands nothing ```: the pattern names no pack; name an `auto...` parameter.
- `an expansion does not nest`: a `...` inside a pattern.

The pack and its loop: `smith-lang-rest`.
