---
name: smith-dev-syntax-control
description: "New syntax (#1428): `if`/`else if`/`else` as values, one-line and indented blocks, `if let`, exhaustive `match` with `:` arms, `while`, `defer` (one line or a block), `errdefer`, `try` on results and options; no `switch`, `goto`, `?:` or `++`. Load when writing control flow in the new syntax."
---

# Control flow

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428. Lines and blocks are in [smith-dev-syntax-values](../smith-dev-syntax-values/SKILL.md); loops in [smith-dev-syntax-loops](../smith-dev-syntax-loops/SKILL.md).

## if and match

```smith
let sign = if x < 0: -1 else if x == 0: 0 else: 1     // if is a value; there is no ?:
if let .some(first) = first_of(xs): use(first) else: report_empty()

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
    .green, .blue: "go"                               // several variants share an arm
```

`match` is exhaustive with no fall-through, over enums, payload enums, `?T` and results. C's `switch` is an error whose fix is `match`. There is no `goto`, no labels, and `++`/`--` are errors whose fix is `+= 1`.

## defer, errdefer and try

```smith
fn load(path: str) -> Result(Config, files.Error):
    let text = try files.read_file(path)      // returns the error from a Result
    defer mem.free(text)                      // runs on every way out of the block
    let cfg = try parse(text)
    errdefer cfg.close()                      // runs only when the block leaves with an error
    try cfg.validate()
    cfg

fn first_word(s: str) -> ?str:
    let i = try find(s, ' ')                  // in a function returning ?T, try on a ?T returns .none
    s[0:i]

fn work(m: *mut Mutex):
    lock(m)
    defer:                                    // several lines
        unlock(m)
        log("done")
    step()
```

## Not settled yet

The value of an `if` with no `else`.
