---
name: smith-dev-syntax-bindings
description: "New syntax (#1428, #2494): `.tuple(tuple) := base.kind else: return .none`, `first := opt else: return`, the binding named after its variant, both errors, what the fixer rewrites (`if k == .v:`, `if k != .v: return`, chains `a || k != .v`, `match` arms and a `switch` on a Result that read `k.v`), `.v(mut x)`. Load when a payload is read after a variant test in the new syntax."
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
- Reading a payload with `.` is an error (`kind.tuple.elements`, `started.ok`). The fixer rewrites, each read `k.v` becoming `v`:
  - `if k == .v:` with reads in the `then` → `if .v(v) := k:`.
  - `if k != .v: return X` (or `break`, `continue`) with reads to the end of the block → `.v(v) := k else: return X` (#2503). Only once the text reads `if test:`: while it still has C's parentheses the statement's span does not start at its `if`.
  - A chain `if a || k != .v || b: X` → `if a: X`, `.v(v) := k else: X`, `if b: X` (`b` may read the payload).
  - A `match` arm `.v:` that reads `m.v` → `.v(v):`. A C `switch` on a Result becomes such a `match` with `.ok(ok)` and `.error(error)` and no `_` arm; its last case needs no `break`, and a `break` of a loop inside a case is the loop's.
  - Left as it is: a write through `k.v` (`k.v.f = x`, `&k.v.f`, `k.v.n += 1`), a write to `k`, a local named `v` anywhere in the function, and a test joined with `&&`.
- `mut` before a bound name lets it change: `.circle(mut circle) := shape else: return`. Without it the name is constant.
- A Result is matched like a payload enumeration: `match r:` with `.ok(ok):` and `.error(error):` (both, or a `_`), and `.ok(ok) := r else: ...`. The checker takes it; the new emitter does not emit a Result yet.

