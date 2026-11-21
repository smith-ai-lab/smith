---
type: specification
title: Functions over code
description: A function that receives the checked code of an argument when the program is built and returns code that takes the place of the call, which is how expect is a library function.
status: partial
built:
  - "the core term as data, in `bootstrap/src/core.rs`: the nodes `lit`, `name`, `call`, `apply`, `lambda`, `bind`, `make`, `field`, and `choose`, each holding its type and its place in the source, with their parts and a one-line printing; `bool` is an enum of the prelude and has no node of its own, and `for` and `loop` are not nodes yet"
  - "the checker produces the core term of a function of the file that is made of whole numbers and `bool`: a number, a name, `+`, `-`, `*` as calls of operators with the exact range, the six comparisons, `and`, `or`, `not`, `if`, a binding, a call of a function like it, a text, a struct or a tuple that is built, a case of an enum with what it holds, a field of a struct or a tuple, and a `match` on any of these by number, text, name, case, and tuple patterns; a list that is built, a function written where it is used, and a call of a name that holds a function, as `lambda` and `apply`; a `loop` over a range or a list, or open-ended, whose round is names given values, `var`s assigned and `if` with no `else` that ends in `break`, with the `var`s it assigns handed from round to round; a function with anything else, such as an assignment in a branch that does not end in `break`, a `return` in a loop, an index, or a type parameter, has none yet"
---

# Functions over code

A function over code receives the code of an argument instead of its value. It runs when the program is built, and the code it returns takes the place of the call. This is how the library provides what would otherwise be built into the compiler.

## A call that receives code

`expect` is a function over code. It takes a check as a lambda and, when a comparison fails, reports both of its sides, each written by [`Repr`](repr.md).

```
fn test_tokens(text: str) -> Result[(), Failed] {
  tokens = encode(text)
  try expect { len(tokens) <= len(text) }
  expect { decode(tokens) == text }
}
```

When the second check fails, it reports the code of the comparison and what each side was.

```
tokenizer_test.smith:4: expected decode(tokens) == text
  left:  "helo"
  right: "hello"
```

A call looks like any other call. `expect` is given one lambda, so it is written without parentheses, as [Lambdas](lambdas.md) says. What `expect` receives is the code `decode(tokens) == text`, and that is how it can report both sides.

## The declaration

The declaration shows which parameters receive code. A parameter of type `Code[T]` receives the checked code of a `T`.

```
fn expect(check: Code[fn() -> bool]) -> Code[Result[(), Failed]]
```

## What a function over code does and does not

- **The argument is checked as a `T` first.** `expect { 3 }` is an ordinary type error: the lambda must give a `bool` and gives a number. It is refused. The compiler does not build this refusal, so no words are quoted.
- **It runs when the program is built, and it carries no effect.** It reads nothing, writes nothing, and ends. The signature of `expect` lists no effect.
- **When it returns `Code[R]`, the call is an `R`.** The returned code is checked as an `R`. In `test_tokens`, the first `expect` is a `Result[(), Failed]`, so `try` takes it, and the second is the value of the function.
- **It sees only what it is passed.** `expect` never sees the line `tokens = encode(text)`, only the code of its lambda. It adds no syntax: a call of a function over code is a call.

## Code built from code

`code` is itself a function over code. It returns the code of its lambda, and inside that lambda `run(c)` stands for the code `c`. `both` receives two checks and returns the code of one expression.

```
fn both(first: Code[fn() -> bool], second: Code[fn() -> bool]) -> Code[bool] {
  code { run(first)() and run(second)() }
}
```

A call of `both` is the expression `run(first)() and run(second)()` with the two checks in place of `first` and `second`. The names in those checks are the names of the function that called `both`.

```
use std.bits (i32)

fn ordered(a: i32, b: i32, c: i32) -> bool {
  both(first: { a < b }) { b < c }
}
```

`ordered` means `a < b and b < c`. The first check is passed by name and the last after the call, as [Lambdas](lambdas.md) says of a call with two lambdas.

## What code is

Code is data. A `Code[T]` is a value of the compiler's own typed core term, which is an ordinary enum, so a function over code looks inside the code it is given with [`match`](match.md) and builds code with the enum's cases or with `code { ... }`. Nothing is built into the compiler for it that the compiler does not use itself.

The core term is what a program is once it has been checked. It is small: everything else the language has is one of these by then, so an operator, an index, `try`, and `convert` are each a `call`, the block after a `with` is a `lambda`, and `if` is a `choose`.

| Node | Is | What becomes it |
| --- | --- | --- |
| `lit` | A literal | `5`, `2.5`, `"hi"` |
| `name` | A parameter or a binding | `x` |
| `call` | A call of a function that has a name, with its types settled | `f(x)`, `a + b`, `x[i]`, `try e` |
| `apply` | A call of a function held in a value | `callback(x)` |
| `lambda` | A function written where it is used | `fn(x) { ... }`, the rest of a block after `with` |
| `bind` | A name given a value, and then what follows | `y = e` and the lines after it |
| `make` | A struct, a tuple, a case of an enum, or a list that is built | `Point(x: 1, y: 2)`, `.some(v)`, `[a, b]` |
| `field` | A part that is read | `p.x` |
| `choose` | One block picked by a pattern | `if`, `match` |
| `for` | An array described by its indices | `for i { ... }`, `x + y` of two arrays, `a @ b` |
| `loop` | Rounds in order, each handing values to the next | `loop`, and a `var` that one assigns |

Each node holds its type and the place in the source it came from. There is no node for an assignment: a `var` that a loop assigns is a value the loop hands from one round to the next.

A `loop` holds what it runs over (a range, a list, or nothing when it is open-ended), the name each round gives the next of it, the `var`s it assigns each with what it held when the loop began, and the body of a round. The body gives the values of those `var`s for the next round, or, where it ends in `break`, the values the loop ends with. The loop is the tuple of the values it ended with. A `var` that the loop assigns is, after the loop, bound again to its part of that tuple.

A name in a `call` holds what it names: the signature of the function, and its body as a core term where the source of it is there. A type holds its fields or its cases. So a function over code sees only what it is passed, and what it is passed is enough to follow a call into the function that is called, and to read the fields of a struct. That is how [`grad`](differentiation.md) differentiates through the functions `f` calls, and how `derive` writes an `impl` field by field.

The names of the cases and what each holds are settled when the checker is written in Smith, as they are the checker's own.

## Other functions over code

[`grad`](differentiation.md) and [`derive`](operators.md#derive) are functions of this kind too.

## The forms

| Written | Is |
| --- | --- |
| `p: Code[T]` | A parameter that receives the checked code of a `T` |
| `-> Code[R]` | A function whose call is an `R`, the code it returns |
| `code { e }` | The code of `e` |
| `run(c)` inside `code` | The code `c` |
| `match c { ... }` on a `Code[T]` | Looks inside the code, by the cases of the core term |

Next: [Differentiation](differentiation.md)
