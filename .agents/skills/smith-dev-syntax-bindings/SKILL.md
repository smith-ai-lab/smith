---
name: smith-dev-syntax-bindings
description: "New syntax (#1428, #2494): `.tuple(tuple) := base.kind else: return .none`, `first := opt else: return`, the binding named after its variant, both errors, what the fixer rewrites (`if k == .v:` and `else if`, `p.*`, `if k != .v: return`, chains `a or k != .v`, `match` arms that read `k.v`), `.v(mut x)`. Load when a payload is read after a variant test in the new syntax."
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
- Reading a payload with `.` is an error (`kind.tuple.elements`). A test narrows a field path as it does a name (#3587: `if p.kind is not .v: return`, then `p.kind.f` reads the payload of `.v`); `p.kind.v.f` after such a test is an error whose fix writes `p.kind.f`. The fixer rewrites, each read `k.v` becoming `v`:
- A narrowed name is the whole value where its enumeration type is expected, and the payload everywhere else (#3634 D23, #3664): the place decides. An argument, a `return`, an assignment, a field of a construction and a comparison with another value of the enumeration read the whole value; every other read is the payload. It covers a narrowed field path (`e.kind`) the same way, and it does not change an option or a `Result`.
  ```
  if l == .integer and r == .integer:
      total = l + r          # l is the i64 payload
      return wrap(l)         # wrap expects a Value: l is the whole Value
  ```
  - A test of a field path (`a.kind == .v`, `!=`, an `else`, an early `return`, `and`/`or`/`not`) stays as written and binds nothing; each read `a.kind.v.f` is written `a.kind.f` (#3598: one rule, whichever test narrowed the path). A name of a payload enumeration whose variant's payload is a record is read the same way (#3631: `if k != .v or k.v.f:` stays and the reads are `k.f`); a name keeps its binding (next item) when the payload is no record (a scalar, an enumeration, a pointer), has a field named like its variant, or the variant is a `Result`'s `ok` or `error`. Where the path stays unnarrowed (a loop that may write its root, a root still in today's syntax) the site is bound as for a name, and a write through the payload (`a.kind.v.f = x`) is `fix_place`'s.
  - `if k == .v:` with reads in the `then` → `if .v(v) := k:`, with or without an `else`: each test of an `else if` chain binds its own payload.
  - Through a pointer: `if p.* == .v:` or `match p.*:` with reads `p.v` → `if .v(v) := p.*:` and `.v(v):` (#2494).
  - `if k != .v: return X` (or `break`, `continue`) with reads to the end of the block → `.v(v) := k else: return X` (#2503). Only once the text reads `if test:`: while it still has C's parentheses the statement's span does not start at its `if`.
  - A chain `if a or k != .v or b: X` → `if a: X`, `.v(v) := k else: X`, `if b: X` (`b` may read the payload).
  - A `match` arm `.v:` that reads `m.v` → `.v(v):`. A C `switch` becomes a `match` also when its last case ends without a `break` and when a case holds a loop's `break`; one whose cases are `.ok` and `.error` gets no `_` arm.
  - A test that starts an `and` chain, in an `if` or a `while` (D12, owner 2026-11-06: "We want to be able to do the and chain"): `if k == .v and k.v.f == x:` → `if .v(v) := k and v.f == x:`; `while k == .v and k.v.n < 3:` → `while .v(v) := k and v.n < 3:`. The chain starts with the pattern, and the value of each pattern ends at the `and`; a later conjunct may be a pattern too: `if .call(call) := k and .name(name) := call.callee.kind and name.n > 1:`, and each name is in scope to its right and in the body. The name is in scope in the rest of the chain and the body, not in the `else` (there it is an unknown name). The checker and the emitter see `if .v(v) := k: if rest: X else: E else: E`, so `E` runs once by either path.
  - A test in a value (D16, #2648; owner 2026-11-07: "A"): a pattern is a conjunct of any `and` chain where a bool is expected (`return`, an initializer, an argument), with the scoping of the `if` chain; the parser reads it as a `pattern_test` expression (#2649), the checker and the emitter type it ("not yet supported" until then). `return e.kind == .binary and e.kind.binary.op == .add` → `return .binary(binary) := e.kind and binary.op == .add`, also for `z := ...` and `z: bool = ...`; a chain with a top-level `or` or a read to the left of the test stays.
  - A `Result` guard that leaves: `if lexed == .error: return source` with `lexed.ok` read below → `.ok(ok) := lexed else: return source` and `ok` for each read. The body may use the result as a whole value (`result = lexed`, `return unknown(lexed)`; in the `else` the name is the result still) but not read a part of it (`lexed.error`), and `return lexed` stays `try`'s unless `lexed` is `mut`. The mirror is bound the same way: `if r == .ok:` or `if r != .error:` that leaves, with `r.error` read below → `.error(error) := r else:` and `error` for each read. A name the function holds already (an earlier guard's binding, a local) makes the binding `ok_value` or `error_value`; only one guard of a function is bound in one pass.
  - A write through the payload (D21, owner 2026-11-07: "Sorry, I meant C."): matching a pointer to a payload enumeration binds pointers into it, so `if one == .index: one.index.index = take(one.index.index)` → `if .index(index) := &one: index.index = take(index.index)`, and a write through `index` changes `one`. `*mut Index` when `one` is `mut`, `*Index` otherwise. Matching a value (`:= one`, `:= p.*`) still binds copies and `.v(mut x)` is a mutable copy: there are no hidden references. The pass (`fix_place`) covers an `if` with a block body, every `k.v` followed by `.field`, one of them written, and writes the form only once the parser reads it.
  - Writing the old-syntax source so the fixer binds it (#2669): one guard `if (x->kind != .v) return;` (the form `!(x->kind == .v)` is not read), the alias not reassigned, no local, parameter or variant of the name `v` (a parameter `op` clashes with `.op`; a parameter `function` with `.function`). Several reads of one payload in an expression go through a one-guard accessor function (`binary_left(e)`); a payload enum built in a literal is built in a local (`DeclKind k = {...}`).
  - A variant test after the first conjunct of an `and` chain with no `or` (lead, 2026-11-11, #3083): the test becomes a pattern where it stands, `if added == .ok and found == .ok: run(found.ok)` → `if added == .ok and .ok(ok) := found: run(ok)`; the conjuncts before it still run first, in order. Where the variant's name is already in the function or is an imported module or alias, the binding is `<variant>_value`.
  - A Result's error handled where it is tested by one line that leaves, with the value read below (lead, 2026-11-11, #3083): `if .error(error) := r: return error.code` then `r.ok` → `ok := match r:` with `.ok(value): value` and `.error(error): return error.code`, each read `r.ok` becoming `ok`; only when `ok` and `value` are not in the function.
  - Left as it is: a write of the whole payload (`k.v = x`), a write to `k` or its address taken in the body, a local named `v` anywhere in the function (for a test inside a chain, `<variant>_value` too), a chain with an `or`, and an error body of more than one line before a read of the value (a block arm that ends in `return` is typed `Void` today). A written chain may start with tests: `if ok and .v(x) := k and x > 1:` is valid (lead, 2026-11-07); `ok` runs first and is not in scope of the pattern's name, and `a or .v(x) := k` is still an error.
- `mut` before a bound name lets it change: `.circle(mut circle) := shape else: return`. Without it the name is constant.
- A `match` on a Result binds its payload in the arm (`.ok(n):`, `.error(e):`) and must cover both.

