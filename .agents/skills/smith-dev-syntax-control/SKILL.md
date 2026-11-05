---
name: smith-dev-syntax-control
description: "New syntax (#1428): `if`/`else if`/`else` as values, one-line and indented blocks, `if let`, `while let`, exhaustive `match` with `:` arms, `while`, `defer` (one line or a block), `errdefer`, `try` on results and options; no `switch`, `goto`, `?:` or `++`. Load when writing control flow in the new syntax."
---

# Control flow

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428. Lines and blocks are in [smith-dev-syntax-values](../smith-dev-syntax-values/SKILL.md); loops in [smith-dev-syntax-loops](../smith-dev-syntax-loops/SKILL.md).

## if and match

```smith
let sign = if x < 0: -1 else if x == 0: 0 else: 1     # if is a value; there is no ?:
if let .some(first) = first_of(xs): use(first) else: report_empty()
while let .some(n) = todo.pop(): visit(n)             # loops while the pattern matches

if total > limit:
    report(total)
else:
    keep(total)

let a = match s:
    .circle(c):
        let r = c.radius
        TAU / 2 * r * r
    .rect(r): r.w * r.h
    .empty: 0
let word = match color:
    .red: "stop"
    .green, .blue: "go"                               # several variants share an arm
```

`match` is exhaustive with no fall-through, over enums, payload enums, `?T` and results. C's `switch` is an error whose fix is `match`. A `match` also takes constants (owner, 2026-11-05; the parser reads them, the checker and emitter not yet, #2328): `kind_a: x`, `0, 1: y`, `_: return z`; a bare name in a pattern is an existing constant, never a binding, `_` is the default (required unless every value is covered), and an arm body is an expression, a `return`/`break`/`continue` or an indented block. An arm that does nothing is written `labels: ()` (`.integer, .floating, .name: ()`); a `break` or `continue` in an arm acts on the enclosing loop, never on the match, so the fixer drops a switch's ending `break` and writes `()` when nothing is left (lead, #2329). C's `switch` with no fall-through is an error whose fix is such a `match`.

## defer, errdefer and try

```smith
fn load(path: str) -> Result(Config, files.Error):
    let text = try files.read_file(path)      # returns the error from a Result
    defer mem.free(text)                      # runs on every way out of the block
    let cfg = try parse(text)
    errdefer cfg.close()                      # runs only when the block leaves with an error
    try cfg.validate()
    cfg

fn first_word(s: str) -> ?str:
    let i = try find(s, ' ')                  # in a function returning ?T, try on a ?T returns .none
    s[0:i]

fn work(m: *mut Mutex):
    lock(m)
    defer:                                    # several lines
        unlock(m)
        log("done")
    step()
```

## Not settled yet

The value of an `if` with no `else`.
