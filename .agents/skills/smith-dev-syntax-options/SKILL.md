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
and `x.some` to `x`). Results get no short form and no narrowing: use `try`, `match` or `if .ok(v) := r:`.

`x is T`, `x is not T`, `x is .v` and `x is not .v` (#3225, D28) are boolean tests at the level of `==`; the right side
is a type or a symbol, and it is an error when `x` can never be it. They narrow the way `==` and `!=` do, and `not`,
`and`, `or` (when false: `x is not .v or use(x)`), an `if` / `else if` / `else` chain (the final `else` takes what is
left) and an early `return` carry the narrowing on. A payload enumeration narrowed to a variant is the payload: after
`if x is not .unary: return`, `x.op` reads the `unary` payload. `x == .v` over a payload enumeration does not narrow yet
(the fixer still writes the binding); the emitter compiles `is` and `is not` to one compare of the tag (#3226).
