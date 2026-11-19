---
type: specification
title: Functions
description: Typed parameters, a return type, a block whose last expression is its value, named arguments, defaults, and comments.
status: partial
built:
  - "typed parameters and return types over `str`, `bool`, and the types a program defines, blocks, named arguments, defaults, a final lambda parameter after them, a name for the result, `requires` and `ensures` over whole numbers, and `//` comments"
---

# Functions

A function is a named block of code that takes typed parameters and gives back a value.

## The smallest function

A function is `fn`, its name, its parameters in parentheses, `->` and the type it gives, and a block for a body. Each parameter is a name, a colon, and a type.

```
// The area of a rectangle.
fn area(w: f32, h: f32) -> f32 { w * h }
```

A call writes the name and an argument for each parameter, in order. `area(2.0, 3.0)` is `6.0`.

A body is a block, and the last expression of a block is its value, so `area` gives `w * h`. A block may have several lines. Each line is a statement, and the last one is the value.

```
use std.console (console, print)

fn echo(text: str) -> str ! console {
  print(text)
  text
}
```

`echo("hi")` prints `hi` and gives `"hi"`.

A function that does something outside itself says so after its type, as `! console` does here. That is an [effect](effects.md).

## A function that gives nothing

A function with nothing to give leaves out `->` and the type. Its body gives nothing, and so does a call to it.

```
use std.console (console, print)

fn say(text: str) ! console {
  print(text)
  print(text)
}
```

`say("hi")` prints `hi` twice. Using what it gives, or ending its body with a value, is refused.

```
fn keep(text: str) { text }
```

This gives "`keep` returns nothing, and its body ends with text". And `print(say("hi"))` gives "`say` returns nothing, where text is needed".

## What the body must give

The body of a function that states a type gives that type.

```
fn is_empty(text: str) -> bool { text }
```

This is refused: "this gives text, where a `bool` is needed". A body with no expression in it is refused too: "`is_empty` returns `bool`, and its body is empty, which gives nothing".

## Calling with the wrong arguments

A call gives one argument for each parameter, of the type the parameter states.

```
fn first(a: str, b: str) -> str { a }
```

- `first("x")` is refused: "`first` needs an argument for `b`".
- `first("x", "y", "z")` is refused: "`first` takes 2 arguments, and this is one more".
- `first(true, "y")` is refused: "this gives a `bool`, where text is needed".

Arguments are evaluated from left to right, in the order they are written.

```
use std.console (console, print)

fn noisy(text: str) -> str ! console {
  print(text)
  text
}

fn main() ! console {
  print(first(noisy("one"), noisy("two")))
}
```

This prints `one`, `two`, and `one`.

## Named arguments

An argument may be named, as in `area(w: 2.0, h: 3.0)`. A call that names its arguments gives the same value as one that does not, and either way of writing it is allowed. Named arguments are written in any order, and are evaluated in the order they are written.

```
use std.console (console, print)

fn main() ! console {
  print(first(b: "y", a: "x"))
}
```

This prints `x`.

Arguments by position come first, in the order of the parameters. Once one argument is named, every argument after it is named too, as in `area(2.0, h: 3.0)`.

- `first(b: "y", "x")` is refused: "an argument by position comes after a named one", with the fix "name it, or move it before the named arguments".
- `first("x", width: "y")` is refused: "`first` has no parameter named `width`".
- `first("x", a: "y")` is refused: "the `a` argument is given twice".

## Defaults

A parameter may have a default, written after its type with `=`. A call that leaves the argument out gets the default.

```
fn scale(x: f32, by: f32 = 2.0) -> f32 { x * by }
```

`scale(3.0)` is `6.0`, and `scale(3.0, 10.0)` and `scale(3.0, by: 10.0)` are both `30.0`.

A call that names an argument may skip one before it. Here `m` is set, and `n` keeps its default.

```
use std.console (console, print)

fn show(label: str, n: str = "x", m: str = "y") ! console {
  print(label)
  print(n)
  print(m)
}

fn main() ! console {
  show("a", m: "Z")
}
```

This prints `a`, `x`, and `Z`. `show("a")` prints `a`, `x`, and `y`, and `show("a", "N")` prints `a`, `N`, and `y`.

Two rules keep defaults simple.

- Parameters with defaults come after those without. The one exception is a final lambda parameter, described under [`with`](with.md). `fn f(a: str = "p", b: str)` is refused: "`b` has no default, and comes after `a`, which has one", with the fix "move `b` before `a`, or give it a default".
- A default is a fixed value. It is an expression that uses no parameter, and it is worked out when the program is built, so it never depends on when or where the call happens. `fn tag(label: str, n: str = label)` is refused: "a default uses no parameter, and `label` is one".
- A default carries no effect. A list that holds elements needs `alloc`, so `fn f(a: List[u8] = [1, 2])`, with a call of `f()` in `main`, is refused: "a default carries no effect, and a list that holds elements needs the `alloc` effect", at the `[`. So is a text that holds an expression, and so is a call of `push`. `[]` needs none, and is allowed.

The compiler fills in a missing argument where the call is written. The function always receives every argument, so a default costs nothing at run time.

## Names and order

A function may be called above the place where it is written.

```
use std.console (console, print)

fn main() ! console {
  print(before())
}

fn before() -> str { "later" }
```

This prints `later`. A function name is defined once, and so is a parameter name within its function. A function cannot take a name the file already has, from another definition, from a `use`, or from [the prelude](prelude.md#a-name-means-one-thing).

- A second `fn first` is refused: "there is already a function named `first`".
- `fn pair(x: str, x: str)` is refused: "`x` is already defined in `pair`".

## Comments

A comment starts with `//` and stands on its own line. It describes the definition or statement that follows it. A comment after code on the same line is refused: "a comment must stand on its own line".

```
// Gives the first of two words.
fn first(a: str, b: str) -> str { a }
```

A comment may describe one parameter, when the parameters stand on lines of their own.

```
// Gives the first of two words.
fn first(
  // The word that is given.
  a: str,
  // The word that is left out.
  b: str,
) -> str { a }
```

## The forms

| Written | Is |
| --- | --- |
| `fn f(a: T, b: U) -> V { e }` | A function that gives a `V` |
| `fn f(a: T) { e }` | A function that gives nothing |
| `fn f(a: T) -> V ! console { e }` | A function with an effect |
| `f(x, y)` | A call with arguments by position |
| `f(x, b: y)` and `f(b: y, a: x)` | A call with named arguments, after any by position |
| `fn f(a: T, b: U = e) -> V { e }` | A function whose `b` defaults to `e` |
| `// text` | A comment, on its own line |

Next: [Bindings](bindings.md)
