---
name: smith-dev-syntax-control
description: "New syntax (#1428): no semicolons (Go's line rule), `if`/`else` and blocks as values, `if let`, exhaustive `match` with `=>`, `while`, `for x in xs`, `enumerate`/`zip`/`range`/`refs` streams, `for` as an expression that builds an array, `continue` as a filter, fused reductions, `defer`, `try`. Load when writing statements or loops in the new syntax."
---

# Statements, control flow and iteration

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Lines and blocks

- No semicolons: a line ends a statement after a name, a literal, `)`, `]`, `}`, `return`, `break` or `continue`; it continues only when it ends with an operator. No parentheses around conditions, braces always, `} else {` on one line.
- `{` only ever starts a block. After `if cond`, `else`, `while cond`, `for ... in xs`, `match x`, `=>` or a signature it is that construct's body; right after a call's `)` on the same line it is a trailing block; anywhere else a block value.

```smith
total := price * count +
    shipping                     // continues: the line ended with an operator

sign := if x < 0 { -1 } else if x == 0 { 0 } else { 1 }   // if is a value; there is no ?:

area := {
    w := right - left
    h := bottom - top
    w * h                        // a block's value is its last line
}

if let .some(first) = first_of(xs) { use(first) } else { report_empty() }
```

## match

```smith
match s {
    .circle(c) => TAU / 2 * c.radius * c.radius
    .rect(r) => r.w * r.h
    .empty => 0
}

match color {
    .red => "stop"
    .green, .blue => "go"        // several variants share an arm
}
```

`match` is exhaustive with no fall-through, over enums, payload enums, `?T` and results. C's `switch` is an error whose fix is `match`. There is no `goto` and there are no labels.

## defer and try

```smith
fn load(path: str) -> Result(Config, files.Error) {
    text := try files.read_file(path)          // returns the error from a Result
    defer mem.free(text)                       // runs on every way out of the block
    parse(text)
}
```

## Not settled yet

`try` on an option, `errdefer`, `defer { ... }` with a block, the value of an `if` with no `else`.
