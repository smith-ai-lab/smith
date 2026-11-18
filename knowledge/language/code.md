---
type: specification
title: Functions over code
description: A function that receives the checked code of an argument when the program is built and returns code that takes the place of the call, which is how expect is a library function.
status: planned
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
fn expect(check: Code[() -> bool]) -> Code[Result[(), Failed]]
```

## What a function over code does and does not

- **The argument is checked as a `T` first.** `expect { 3 }` is an ordinary type error: the lambda must give a `bool` and gives a number. It is refused. The compiler does not build this refusal, so no words are quoted.
- **It runs when the program is built, and it carries no effect.** It reads nothing, writes nothing, and ends. The signature of `expect` lists no effect.
- **When it returns `Code[R]`, the call is an `R`.** The returned code is checked as an `R`. In `test_tokens`, the first `expect` is a `Result[(), Failed]`, so `try` takes it, and the second is the value of the function.
- **It sees only what it is passed.** `expect` never sees the line `tokens = encode(text)`, only the code of its lambda. It adds no syntax: a call of a function over code is a call.

## Code built from code

`code` is itself a function over code. It returns the code of its lambda, and inside that lambda `run(c)` stands for the code `c`. `both` receives two checks and returns the code of one expression.

```
fn both(first: Code[() -> bool], second: Code[() -> bool]) -> Code[bool] {
  code { run(first)() and run(second)() }
}
```

A call of `both` is the expression `run(first)() and run(second)()` with the two checks in place of `first` and `second`. The names in those checks are the names of the function that called `both`.

```
fn ordered(a: i32, b: i32, c: i32) -> bool {
  both(first: { a < b }) { b < c }
}
```

`ordered` means `a < b and b < c`. The first check is passed by name and the last after the call, as [Lambdas](lambdas.md) says of a call with two lambdas.

## Other functions over code

[`grad`](differentiation.md) and [`derive`](operators.md#derive) are functions of this kind too.

## The forms

| Written | Is |
| --- | --- |
| `p: Code[T]` | A parameter that receives the checked code of a `T` |
| `-> Code[R]` | A function whose call is an `R`, the code it returns |
| `code { e }` | The code of `e` |
| `run(c)` inside `code` | The code `c` |

Next: [Differentiation](differentiation.md)
