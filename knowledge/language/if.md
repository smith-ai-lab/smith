---
type: specification
title: If
description: if as an expression that chooses between blocks, else if, and the words not, and, and or that combine conditions.
status: partial
built:
  - "`if` with and without `else`, `else if`, and `not`, `and`, and `or`"
---

# If

`if` chooses between two blocks by a condition, and gives what the block that runs gives.

```
fn relu(x: f32) -> f32 {
  if x < 0.0 { 0.0 } else { x }
}
```

`relu(-2.0)` is `0.0` and `relu(3.0)` is `3.0`. The condition comes after `if`, with no parentheses, and the blocks are in braces. An `if` is an expression, so it may stand anywhere a value may: as the last line of a function, as an argument, or as the value of a [binding](bindings.md).

```
fn sign(n: i32) -> str {
  if n < 0 { "negative" } else { "not negative" }
}

fn main() ! console {
  word = if sign(-3) == "negative" { "yes" } else { "no" }
  print(word)
  print(sign(4))
}
```

This prints `yes` and then `not negative`.

## The condition is a `bool`

A condition is a `bool`, such as what `<`, `<=`, `>`, `>=`, `==`, and `!=` give. Anything else is refused.

```
fn main() ! console {
  if 1 { print("x") }
}
```

This gives "this is a number, where a `bool` is needed". A name of another type gives "this gives a `i32`, where a `bool` is needed".

## Both blocks give the same type

With an `else`, the `if` gives what the block that ran gives, so both blocks give the same type.

```
fn main() ! console {
  word = if true { "a" } else { true }
  print(word)
}
```

This gives "this gives a `bool`, and the first block gives text, where the blocks of an `if` give the same type". When the function states the type, the message names it instead.

```
fn describe(n: i32) -> str {
  if n < 0 { "negative" } else { 7 }
}
```

This gives "this is a number, where text is needed".

## An `if` with no `else`

An `if` with no `else` gives `()`, and what its block gives is not read. It is for what its block does.

```
fn main() ! console {
  if true { print("shown") }
  if false { print("hidden") }
}
```

This prints `shown`. Its block may end with a value, as `if true { "a" }` does, and the value is dropped. Using the `if` where a value is needed is refused.

```
fn describe(n: i32) -> str {
  if n < 0 { "negative" }
}
```

This gives "this gives nothing, where text is needed".

## Changing a `var`

A block may assign to a [`var`](var.md) of the function around it, which is how a block does something to the function. The name it assigns is the one defined outside.

```
fn main() ! console {
  var word = "a"
  if true { word = "b" } else { word = "c" }
  print(word)
}
```

This prints `b`. A name defined inside a block can be used only inside it, as [Bindings](bindings.md) says.

## More than two choices

`else` may be followed by another `if`, which makes a chain. The first condition that holds picks the block, and the last `else` runs when none does.

```
fn grade(n: i32) -> str {
  if n < 50 { "fail" } else if n < 80 { "pass" } else { "distinction" }
}
```

`grade(10)` is `"fail"`, `grade(60)` is `"pass"`, and `grade(90)` is `"distinction"`. As for a plain `if`, every block gives the same type, and a chain with no last `else` gives `()`.

The `else` is written on the line where the block before it ends. On a line of its own it is refused: "expected a statement, found `else`".

## Combining conditions

`not`, `and`, and `or` combine conditions into one. They take `bool`s and give a `bool`, and any other operand is refused: `if not "a" { ... }` gives "this gives text, where a `bool` is needed".

```
fn main() ! console {
  a = true
  b = false
  print(if a and not b { "yes" } else { "no" })
  print(if a and b { "yes" } else { "no" })
  print(if a or b { "yes" } else { "no" })
}
```

This prints `yes`, `no`, and `yes`.

`not` binds most tightly, then `and`, then `or`, so `a or b and c` means `a or (b and c)`. Parentheses group the other way, as in `(a or b) and c`.

```
fn main() ! console {
  a = true
  b = false
  c = false
  print(if a or b and c { "yes" } else { "no" })
  print(if (a or b) and c { "yes" } else { "no" })
}
```

This prints `yes`, then `no`.

`and` and `or` evaluate their right side only when the left side has not settled the answer. `and` stops at a `false`, and `or` stops at a `true`. So a right side that prints shows whether it ran.

```
fn loud(text: str, answer: bool) -> bool ! console {
  print(text)
  answer
}

fn main() ! console {
  if loud("one", false) and loud("two", true) { print("both") }
  if loud("three", true) or loud("four", true) { print("either") }
}
```

This prints `one`, `three`, and `either`. `two` and `four` never print.

The words are `and`, `or`, and `not`. `&&`, `||`, and `!` are not in Smith: `true && false` gives "unexpected character `&`", and `!true` gives "expected an expression, found `!`".

## Where `{` opens a body

In the head of an `if`, `{` opens the body. A call there that takes a lambda is put in parentheses, as [Lambdas](lambdas.md) shows.

## The forms

| Written | Is |
| --- | --- |
| `if c { a } else { b }` | `a` when `c` holds, and `b` when it does not |
| `if c { a }` | Runs `a` when `c` holds, and gives `()` |
| `if c { a } else if d { b } else { e }` | The first block whose condition holds, or the last |
| `not c` | `true` when `c` is `false` |
| `a and b` | `true` when both are, and `b` runs only when `a` holds |
| `a or b` | `true` when either is, and `b` runs only when `a` does not hold |

Next: [Structs](structs.md)
