---
type: specification
title: Match
description: match takes an enum or another value apart, with a pattern and a block for each way it can be, patterns for cases, tuples, and literals, and the rules for covering every case, which a range of whole numbers is covered by listing its values.
status: partial
built:
  - "`match` as a statement and as an expression, patterns that name a case, a bare name, a tuple, and a whole number, a character, or a text, written inside a case and inside a tuple too, the first pattern that matches being taken, and an error for a case left out, for a `match` on whole numbers or text with no bare name at the end, and for a pattern the ones before it cover, with a `match` on whole numbers compiled to one jump"
  - "a pattern for a case that holds `()` written without it, as `.more`"
  - "not a `match` on a range of whole numbers that is complete without a bare name"
---

# Match

`match` takes a value apart. It has a pattern and a block for each way the value can be, and it runs the block of the pattern that matches. Its value is what that block gives.

```
fn go(l: Light) -> bool {
  match l {
    .green { true }
    .yellow { false }
    .red { false }
  }
}
```

`go(.green)` is `true` and `go(.red)` is `false`. The value comes after `match`, with no parentheses, and the blocks are in braces, one pattern to a block. In a pattern a case of an [enum](enums.md) is always written with a leading dot.

## A case that holds something

A case that holds a value is written with a pattern for it in parentheses. A bare name there introduces a new variable for what the case holds, and the block can use it.

```
fn sides(s: Shape) -> i32 {
  match s {
    .circle(c) { 0 }
    .rect(r) { 4 }
    .empty { 0 }
  }
}
```

`c` is a `Circle` and `r` is a `Rect`, so what a case holds can be passed on after a match, as `rect_area(r)` would pass the `Rect` to a function. A case that holds nothing is written without parentheses. `.green(n)` is refused: "`.green` holds nothing, so its pattern takes none".

A case the enum does not have is refused: `.blue { ... }` on a `Light` gives "`Light` has no case named `blue`". A case pattern on a value that is not an enum is refused: with `n: i32`, `.green { ... }` gives "`.green` is a case, and this matches a `i32`, which is not an enum".

## A match as a value and as a statement

A `match` whose value is used is an expression, and its blocks give the same type.

```
fn main() ! console, alloc {
  l = Light.red
  word = match l {
    .green { "go" }
    .yellow { "slow" }
    .red { "stop" }
  }
  print(word)
}
```

This prints `stop`. When the blocks give different types, the `match` is refused.

```
fn go(l: Light) -> str {
  match l {
    .green { "go" }
    .yellow { "slow" }
    .red { 1 }
  }
}
```

This gives "this is a number, where text is needed". Used as a statement, nothing its blocks give is read, so they need not agree.

```
fn main() ! console, alloc {
  match Light.red {
    .green { print("go") }
    .yellow { print("slow") }
    .red { print("stop") }
  }
}
```

This prints `stop`.

## Every case is covered

The patterns of a `match` cover each case once. Either each case is written, or a bare name after them covers the rest.

```
fn go(l: Light) -> bool {
  match l {
    .green { true }
    other { false }
  }
}
```

A case left out is refused.

```
fn go(l: Light) -> bool {
  match l {
    .green { true }
    .yellow { false }
  }
}
```

This gives "this `match` does not cover `.red`", with the fix "add a block for `.red`, or end with a bare name that covers the rest". The message names every case that is left out, as "does not cover `.cat(_)`, `.lost(.none)`, and `.none`" does for a deeper enum.

A pattern that the ones before it already cover is refused, since it could never run. That is the reason a bare name goes last.

```
fn go(l: Light) -> bool {
  match l {
    other { false }
    .green { true }
  }
}
```

This gives "this pattern is never reached, as the patterns before it match everything it matches", and so does a case written twice.

A bare name always introduces a new variable, and never names a case. `red { ... }` without a dot is a bare name that happens to be called `red`: it matches everything, and holds the `Light`.

```
fn go(l: Light) -> bool {
  match l {
    .green { true }
    red { false }
  }
}
```

This is allowed, and `red` is the value that is not `.green`. Because it is a new variable, its name must not be one that is already defined: in a function with a parameter `n`, a pattern `n { ... }` gives "`n` is already defined in `f`".

## A pattern inside a pattern

A case's pattern may hold another pattern, for what the case holds. The enum `Option[u8]` holds an `u8` in `.some`.

```
fn describe(found: Option[u8]) -> str {
  match found {
    .some(0) { "the first one" }
    .some(n) { "a later one" }
    .none { "not there" }
  }
}
```

`describe(.some(0))` is `"the first one"`, `describe(.some(3))` is `"a later one"`, and `describe(.none)` is `"not there"`. A pattern may be as deep as the value is.

```
fn inner(x: Option[Option[u8]]) -> u8 {
  match x {
    .some(.some(n)) { n }
    .some(.none) { 1 }
    .none { 0 }
  }
}
```

## Matching a tuple

A tuple is matched by a pattern for each of its parts, and a `match` on a [tuple](tuples.md) lets one `match` look at several values at once.

```
enum Token {
  equal_equal
  arrow
  assign
  single: u8
}

fn two(b: u8, next: u8) -> Token {
  match (b, next) {
    ('=', '=') { .equal_equal }
    ('-', '>') { .arrow }
    ('=', other) { .assign }
    (first, second) { .single(first) }
  }
}
```

`two('=', '=')` is `.equal_equal`, `two('=', 'x')` is `.assign`, and `two('a', 'b')` is `.single('a')`. The pattern has as many parts as the tuple, and `(a, b, c)` on a tuple of 2 is refused: "this pattern takes apart a tuple of 3, and it matches a `(u8, u8)`".

## A literal as a pattern

A literal is a pattern. It matches a value equal to it: a whole number, a character, or a text. It goes wherever a pattern goes, so inside a case and inside a tuple too, as `0` and `'='` do above.

```
fn word(n: u8) -> str {
  match n {
    0 { "zero" }
    1 { "one" }
    other { "many" }
  }
}
```

`word(0)` is `"zero"` and `word(9)` is `"many"`. The first pattern that matches is the one taken, so an earlier pattern wins over a later one that would also match, as `.some(0)` wins over `.some(n)` above. A literal written twice is refused as a case is: `0 { ... }` after `0 { ... }` gives "this pattern is never reached, as the patterns before it match everything it matches".

A list of literals covers a whole number when it lists every value of its range, and a `match` on a range is complete then, with no bare name.

```
fn name(d: 0..3) -> str {
  match d {
    0 { "none" }
    1 { "one" }
    2 { "two" }
  }
}
```

`name(2)` is `"two"`. A bare name after these is refused as a pattern that is never reached, since the patterns before it cover every value.

A value of the range that no literal lists is a case left out.

```
fn name(d: 0..4) -> str {
  match d {
    0 { "none" }
    1 { "one" }
    2 { "two" }
  }
}
```

This is refused, as the `match` does not cover `3`, with the fix to add a block for `3`, or to end with a bare name that covers the rest.

A range of many values is covered by a bare name in practice, as no list is long enough, so a `match` on a `Byte` of 0 and 1 ends with one. Without it, it is refused.

```
fn word(n: u8) -> str {
  match n {
    0 { "zero" }
    1 { "one" }
  }
}
```

This gives "this `match` does not cover the rest of the values", with the fix "end with a bare name that covers the rest". No list of literals covers every text, so the same holds for a text: `"one" { ... }` and `"two" { ... }` with no bare name give the same message.

A literal must be of the type matched: `"zero" { ... }` on a `u8` gives "this is a text, and this matches a `u8`".

A `match` on whole numbers is one jump, however many literals it has.

## The forms

| Written | Is |
| --- | --- |
| `match v { p { e } }` | Runs the block of the first pattern `p` that `v` matches |
| `.a { e }` | A case that holds nothing, or that holds `()`, as [Enums](enums.md#a-case-that-holds-) says |
| `.a(p) { e }` | A case that holds a value that `p` matches |
| `x { e }` | A bare name: matches anything, and is a new variable |
| `(p, q) { e }` | A tuple whose parts `p` and `q` match |
| `0`, `'a'`, `"a"` | A whole number, a character, or a text equal to it |

Next: [Type parameters](type-parameters.md)
