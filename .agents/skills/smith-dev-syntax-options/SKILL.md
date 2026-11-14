---
name: smith-dev-syntax-options
description: "New syntax (#1428, #2481): `if v := find(xs, k):` binds a call's result, `if x != .none:` narrows a `?T` name to `T`, `if x == .none: return`, `contract.assert(x != .none)`, which names and field paths narrow (`mut` names and `a.b` too), no `x.some` or `x == .some`, results get no narrowing. Load when testing or reading an option in the new syntax."
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
leaves (`return`, `break`, `continue`). Every name narrows, `mut` or not (item 55), and so does a field path (`p.child`,
`cur.kind`, `a.b.c`; item 68 of #3516, #3587), through a value or a pointer. A narrowing ends at the next assignment
that could change which case it is: to the name, to the path or to a part of it before the tested field, `&` of any of
those, a call that is passed a writable pointer to it (or the path as a method receiver), or a loop that writes the name
(an assignment to a part below the tested field keeps it). A path through an index narrows too (`xs[i].kind`,
`xs[i + 1].next`, `a.b[2].c`; item 73 of #3516, #3624) when the index is a name, an integer, or one of them plus or minus
another; an index that calls does not narrow. It also ends at an assignment to a name used in the index (`i = i + 1`),
to the array, or to any element of it (`xs[j] = v`), and at a loop that writes the index.
`contract.assert(c)` as a statement narrows the rest of its block as `if not c: return` does (item 72 of #3516, #3622),
for every name and path `c` tests (`contract.assert(x != .none)`, `a.kind is .v and a.n != .none`): use it for a value that is
absent only by a bug, with a comment where the reason is not obvious; where an absent value is a real outcome, test it and
return, and where a flag says the optional is set, make the data a union. The old source spells it `contract::assert(c);`.
There is no `x.some`: after the
test `x` is the value. `x == .some` is written `x != .none`, and `x != .some` is `x == .none` (the fixer rewrites both,
and `x.some` to `x`). Results get no short form and no narrowing: use `try`, `match` or `if .ok(v) := r:`.

`x is T`, `x is not T`, `x is .v` and `x is not .v` (#3225, D28) are boolean tests at the level of `==`; the right side
is a type or a symbol, and it is an error when `x` can never be it. They narrow the way `==` and `!=` do, and `not`,
`and`, `or` (when false: `x is not .v or use(x)`), an `if` / `else if` / `else` chain (the final `else` takes what is
left) and an early `return` carry the narrowing on. A payload enumeration narrowed to a variant is the payload: after
`if x is not .unary: return`, `x.op` reads the `unary` payload. `x == .v` over a payload enumeration name does not narrow yet
(the fixer still writes the binding), but `p.kind == .v` over a field path does, like `is`; reading it with the
variant's name (`p.kind.v.f`) is an error whose fix writes `p.kind.f`; the emitter compiles `is` and `is not` to one compare of the tag (#3226).
