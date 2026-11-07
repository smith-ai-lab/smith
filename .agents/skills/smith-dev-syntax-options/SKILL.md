---
name: smith-dev-syntax-options
description: "New syntax (#1428, #2481): `if v := find(xs, k):` binds a call's result, `if x != .none:` narrows a `?T` name to `T`, `if x == .none: return`, which names narrow (not `mut` names, not fields), no `x.some` or `x == .some`, results get no narrowing. Load when testing or reading an option in the new syntax."
---

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); control flow is in [smith-dev-syntax-control](../smith-dev-syntax-control/SKILL.md).

# Options: bind or narrow

```smith
if v := find(xs, k): use(v)          # a call's result: bind it (sugar for .some(v) :=)
if x != .none: use(x)                # a name that can't change: narrows ?T to T, no second name
if x == .none: return                # early exit; x is T after this line
while n := todo.pop(): visit(n)      # loops and iterators
if .circle(c) := shape: area(c)      # any other enum: the general pattern
```

A `?T` name is a `T` (a `?*T` a `*T`) where it is known not to be `.none`: the block of `if x != .none:`, the right
side of `x != .none and ...`, the `else` of `if x == .none:`, and the lines after an `if x == .none:` whose block always
leaves (`return`, `break`, `continue`). Only names that can't change narrow: parameters and locals without `mut`, not
`mut` names and not fields (`p.child`); those keep the pattern form `if v := p.child:`. There is no `x.some`: after the
test `x` is the value. `x == .some` is written `x != .none`, and `x != .some` is `x == .none` (the fixer rewrites both,
and `x.some` to `x`). Results get no short form and no narrowing: use `try`, `match` or `if .ok(v) := r:`. `or` chains
do not narrow.
