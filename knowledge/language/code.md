---
type: specification
title: Functions over code
description: Functions that receive the checked code of an argument when the program is built and return code, which is how expect and grad are library functions.
status: planned
---

# Functions over code

A function can receive the code of an argument instead of its value. Such a function runs when the program is built, and the code it returns takes the place of the call. This is how the library provides what would otherwise be built into the compiler. `expect` is one: it takes a check as a lambda and, when a comparison fails, reports both of its sides.

```
fn test_tokens(text: str) -> Result[(), Failed] {
  tokens = encode(text)
  try expect { len(tokens) <= len(text) }
  expect { decode(tokens) == text }
}
```

```
tokenizer_test.smith:4: expected decode(tokens) == text
  left:  "helo"
  right: "hello"
```

A call looks like any other call. The declaration shows which parameters receive code, with the type `Code[T]`: the checked code of a `T`.

```
fn expect(check: Code[() -> bool]) -> Code[Result[(), Failed]]
```

- A parameter of type `Code[T]` receives the code of its argument. The argument is checked as a `T` first, so `expect { 3 }` is an ordinary type error.
- A function with a `Code` parameter runs when the program is built. It carries no effect, so it reads nothing, writes nothing, and ends.
- When it returns `Code[R]`, the call is an `R`, and the returned code is checked as one. So `try expect { ... }` works: the call is a `Result[(), Failed]`.
- It sees only what it is passed, never the code around the call, and it adds no syntax.

Code is built from code. `code` is itself a function over code that returns its lambda's code, and inside that lambda `run(c)` stands for the code `c`.

```
fn both(first: Code[() -> bool], second: Code[() -> bool]) -> Code[bool] {
  code { run(first)() and run(second)() }
}
```

`both(first: { a < b }) { b < c }` is the expression `a < b and b < c`, whose names are those of the function that called `both`.

[`grad`](differentiation.md) and `derive` are functions of this kind too.

Next: [Differentiation](differentiation.md)
