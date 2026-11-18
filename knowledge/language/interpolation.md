---
type: specification
title: Strings that hold expressions
description: A string that holds expressions in braces, which the Fmt trait turns into text, and the escapes that write a brace or a quote.
status: partial
built: "strings that hold expressions of any type that implements `Fmt`, an `impl` of `Fmt`, and every escape"
---

# Strings that hold expressions

Every string literal may hold expressions in braces, and there is no other kind of string. Each expression is turned into text by the `Fmt` trait, and the pieces are joined into one `str`.

```
fn greet(name: str, age: i32) ! console, alloc {
  print("hello {name}, you are {age}")
}
```

`greet("Ana", 31)` prints `hello Ana, you are 31`.

- The braces hold any expression of a type that has `Fmt`, as `{2 + 3}` does.
- Nothing is special-cased. A `str` in braces goes through its own `Fmt` implementation, like a value of any other type.
- `print` never formats anything: it only ever receives a `str`.
- A string with an expression in it has a size known only at run time, so it carries `alloc`. A plain string literal does not.
- Empty braces are refused: "expected an expression, found `}`". So is a name that is not defined: "there is no name `missing` defined before this in `main`".

## `Fmt`

`Fmt` is text for a person.

```
trait Fmt[T] {
  fn fmt(value: T) -> str ! alloc
}
```

A `str`, an integer, and a `bool` have it, and the text they give is the value as it reads: `{"inner"}` gives `inner`, `{1 == 2}` gives `false`. A struct, an enum, and a tuple have it too, as [Repr](repr.md) says.

A type that should read differently implements `Fmt` itself.

```
struct Money {
  cents: i32
}

impl Fmt[Money] {
  fn fmt(value: Money) -> str ! alloc { "{value.cents} cents" }
}

fn price(m: Money) ! console, alloc {
  print("{m}")
}
```

`price(Money(cents: 5))` prints `5 cents`.

A type that has no `Fmt` cannot be put in braces. If `Secret` has none, `"{Secret(key: 1)}"` is refused: "there is no implementation of `Fmt` for `Secret`".

There are no format options inside the braces. A width or a number of decimal places is an ordinary function call, as in `"{fixed(x, 2)}"`. A whole number in another base is written with the functions of [Number bases](../library/fmt.md), as in `"U+{hex(code, width: 4)}"`.

## Writing a brace, a quote, and a break

A backslash writes a character that would otherwise mean something else. These escapes are the whole set.

| Written | Means |
| --- | --- |
| `\"` | A double quote |
| `\\` | A backslash |
| `\{` and `\}` | A brace |
| `\n` | A line break |
| `\t` | A tab |

```
fn show(x: i32, y: i32) ! console, alloc {
  print("point \{{x}, {y}\}")
}
```

`show(3, 4)` prints `point {3, 4}`. `print("a\tb\\c \"q\"\nnext")` prints `a`, a tab, `b\c "q"`, and on the next line `next`.

- Any other character after a backslash is refused: "unknown escape: a backslash is followed by `q`, not one of " \ { } n t".
- A brace that is not escaped always opens or closes an expression. A `}` on its own, as in `"a } b"`, is refused: "a `}` in a string closes no expression", with the fix "write `\}` for a brace". A `{` that is never closed, as in `"{x"`, is refused as a string with no closing quote.
- A string literal ends on the line it starts. A string that runs onto the next line is refused: "this string has no closing quote on the line it starts". A line break in the text is written `\n`.

## The forms

| Written | Is |
| --- | --- |
| `"{e}"` | The `Fmt` of `e`, written in place |
| `"\{"` and `"\}"` | A brace |
| `"\n"` and `"\t"` | A line break and a tab |
| `"\""` and `"\\"` | A quote and a backslash |

Next: [Repr](repr.md)
