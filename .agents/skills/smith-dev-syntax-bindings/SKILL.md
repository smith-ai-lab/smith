---
name: smith-dev-syntax-bindings
description: "New syntax (#1428, #2494): `.tuple(tuple) := base.kind else: return .none`, `first := opt else: return`, the binding named after its variant, both errors, what the fixer rewrites (`if k == .v:` and `else if`, `p.*`, `if k != .v: return`, chains `a || k != .v`, `match` arms that read `k.v`), `.v(mut x)`. Load when a payload is read after a variant test in the new syntax."
---

# Refutable bindings (owner, 2026-11-06; #2494)

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); control flow is in [smith-dev-syntax-control](../smith-dev-syntax-control/SKILL.md).

A definition whose pattern can fail takes an `else` that leaves; the names live to the end of the enclosing block.

```smith
fn first_element(base: *Type) -> ?TypeRef:
    .tuple(tuple) := base.kind else: return .none
    first := tuple.elements.get(0) else: return .none   # the option short form: `first := opt` is `.some(first) := opt`
    .named(named) := first.kind else: return .none
    lookup(named.path)

started := process.start(cmd)
.ok(child) := started else:
    log("could not start")
    return started
```

- The binding takes the variant's name: `.tuple(tuple)`, `.error(error)`, `.ok(ok)`. If that name is in scope (shadowing is an error) or is a keyword, the fixer leaves the site as it is.
- A pattern that can fail **must** have `else` and one that always matches (`(q, r) := divmod(a, b)`) **must not**: "this pattern always matches: remove `else`". The `else` is one line or indented and ends in `return`, `break`, `continue` or a call that never returns: "the `else` of a binding must leave: end it with `return`, ...". The names do not exist inside the `else`.
- Passing an error up stays `try`: `child := try process.start(cmd)`.
- Reading a payload with `.` is an error (`kind.tuple.elements`). The fixer rewrites, each read `k.v` becoming `v`:
  - `if k == .v:` with reads in the `then` → `if .v(v) := k:`, with or without an `else`: each test of an `else if` chain binds its own payload.
  - Through a pointer: `if p.* == .v:` or `match p.*:` with reads `p.v` → `if .v(v) := p.*:` and `.v(v):` (#2494).
  - `if k != .v: return X` (or `break`, `continue`) with reads to the end of the block → `.v(v) := k else: return X` (#2503). Only once the text reads `if test:`: while it still has C's parentheses the statement's span does not start at its `if`.
  - A chain `if a || k != .v || b: X` → `if a: X`, `.v(v) := k else: X`, `if b: X` (`b` may read the payload).
  - A `match` arm `.v:` that reads `m.v` → `.v(v):`. A C `switch` becomes a `match` also when its last case ends without a `break` and when a case holds a loop's `break`; one whose cases are `.ok` and `.error` gets no `_` arm.
  - A test that starts an `&&` chain, in an `if` or a `while` (D12, owner 2026-11-06: "We want to be able to do the and chain"): `if k == .v && k.v.f == x:` → `if .v(v) := k && v.f == x:`; `while k == .v && k.v.n < 3:` → `while .v(v) := k && v.n < 3:`. The chain starts with the pattern, and the value of each pattern ends at the `&&`; a later conjunct may be a pattern too: `if .call(call) := k && .name(name) := call.callee.kind && name.n > 1:`, and each name is in scope to its right and in the body. The name is in scope in the rest of the chain and the body, not in the `else` (there it is an unknown name). The checker and the emitter see `if .v(v) := k: if rest: X else: E else: E`, so `E` runs once by either path.
  - Left as it is: a write through `k.v` (`k.v.f = x`, `&k.v.f`, `k.v.n += 1`), a write to `k`, a local named `v` anywhere in the function, and a variant test that does not start its chain (`ok && k == .v`: the conjuncts before it may be what makes `k` safe to read).
- `mut` before a bound name lets it change: `.circle(mut circle) := shape else: return`. Without it the name is constant.
- A `match` on a Result binds its payload in the arm (`.ok(n):`, `.error(e):`) and must cover both.

