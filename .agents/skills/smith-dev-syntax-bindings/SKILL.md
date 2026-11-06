---
name: smith-dev-syntax-bindings
description: "New syntax (#1428, #2494): `.tuple(tuple) := base.kind else: return .none`, `first := opt else: return`, the binding named after its variant, both errors, what the fixer rewrites (`if k == .v:` and `match` arms that read `k.v`). Load when a payload is read after a variant test in the new syntax."
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
- Reading a payload with `.` is an error (`kind.tuple.elements`). The fixer writes `if k == .v:` with reads `k.v.f` as `if .v(v) := k:` with `v.f`, and a `match` arm `.v:` that reads `m.v` as `.v(v):` with `v`. Not fixed yet: `if k != .v: return X` followed by reads, and a `switch` on a `Result` (#2494 follow-up).
- Not built yet: `mut` on a bound name (`.tuple(mut tuple)`).

