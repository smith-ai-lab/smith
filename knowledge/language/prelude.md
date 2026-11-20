---
type: specification
title: The prelude
description: The names that are in scope in every file without a use, and what a file imports for everything else.
status: partial
built:
  - "every name below, `Int` and `Byte` among them, in scope in every file with no import"
  - "not a prelude without functions: today the functions of these traits, as `add`, `div`, `compare`, and `convert`, are bare names in every file, so a file's own function of one of those names is refused"
  - "not `Pos`"
  - "a name of the standard library outside the table, such as `print`, `console`, `push`, `narrow`, a `wrapping_` function, or the name of a size, is refused where a file uses it with no `use`, and the error names the module to import it from"
---

# The prelude

A small set of names is in scope in every file with no `use`. It is called the prelude, and it is as small as it can be: a name is in it only when the syntax of the language means that name, when it is a trait that every type or every function is given with no `impl`, or, for `Int`, `Nat`, `Pos`, and `Byte`, when it says what a number is for. There is no other way in, and so no name is in it for convenience. The syntax means types, traits, and effects, and never a function, so no function is in it.

```
use std.console (console, print)

fn main() ! console {
  print("hello")
}
```

No syntax means `print`, or the `console` effect it needs, so a program that prints imports both, as it imports anything, in the way [Importing with use](use.md) describes. `main`, `fn`, and the string need nothing.

## What is in it, and what means it

| In the prelude | The syntax that means it |
| --- | --- |
| `Int`, `Nat`, `Pos`, `Byte`, the float types, `bool`, `str` | A literal, the size of an array, and a byte of a text |
| `List`, `Slice` | A list written in brackets, as `[1, 2, 3]`, and the piece of one written `x[a..b]`, which is a `Slice`, as a `str` is |
| `Option`, `Result`, `Flow` with its two types, and `Try` | `try` |
| `Order`, `Eq`, and `Ord` | `==`, `!=`, `<`, `<=`, `>`, and `>=` |
| `Add`, `Sub`, `Mul`, `Div`, `Rem`, `Pow`, and `Neg` | The arithmetic operators |
| `Index` | `x[i]` |
| `Iterate`, and `Flow` | `loop v in x`, `break`, and `return` in one |
| `Exit` | What `main` returns |
| `Fmt` and `Repr` | An expression in a string's braces |
| `Convert` | Two types that meet in an operator, and a failure that `try` converts |
| `Hash`, `Copy`, and `Discard` | The traits a type is given with no `impl`, which a bound as `T: Copy` and a `without` name |
| `Function` | The trait every function with a name is given with no `impl`, as [Traits](traits.md) says |
| `FromInt`, `FromFloat`, `FromString`, and `FromArray` | The literals |
| `alloc`, `diverge`, and the other effects the compiler gives | A string that holds an expression, an open-ended `loop`, and the rest of [the built-in effects](provided-effects.md#built-in) |

## A trait's function is written with the trait's name

The syntax means a trait, and what the syntax does is call the trait's function. A program that calls that function itself writes it with the trait's name, which is in scope.

```
fn total(a: Int, b: Int) -> Int { Add.add(a, b) }

fn nearer(a: str, b: str) -> Order { Ord.compare(a, b) }
```

`Add.add(a, b)` is what `a + b` means, and `Ord.compare(a, b)` is what the comparisons ask. An `impl` names its function inside itself, so `impl Add[Rect] { fn add(a: Rect, b: Rect) -> Rect { ... } }` needs nothing in scope but `Add`.

So `add`, `sub`, `div`, `compare`, `convert`, and the others are not names a file has, and a program or a library gives them to its own functions: `div` is an element of [`std.html`](../library/html.md), and a file that imports it divides with `/` as before.

## Everything else is imported

A name that is not in the table is imported. That holds for the rest of the standard library as for a program's own modules.

```
use std.fs (FsError, fs, read_all)
use std.list (push)

fn lines_of(path: str) -> Result[List[Byte], FsError] ! fs, alloc {
  read_all(path)
}
```

`push` grows a list, and no syntax means it, so it is imported. So are `freeze`, `push`, and `copy` of [`std.list`](../library/list.md), and the functions of [`std.int`](../library/int.md), the names of sizes, `u8` to `i64`, and the functions of `Bits`, from [`std.bits`](../library/bits.md), `utf8`, `Map`, and the array functions such as `sum` and `map`. An effect the standard library declares is imported as any effect is, by its name, with each operation the file calls: `fs` and `read_all` above, and `console` and `print` in the first program.

A name used and not imported is refused, and the error names the module to import it from, as [Importing with use](use.md#a-name-that-is-missing) says.

## A name means one thing

A bare name has exactly one meaning in a file. So a file cannot define, or import, a name that it already has, and the names of the prelude are names it already has.

```
struct Option {
  on: bool
}
```

This is refused, as `Option` is the type that `try` means. The same holds for a name that a `use` already brought in. A function named `add` is not refused, as no function is in the prelude: it is the file's own, and `+` still means `Add.add`.

Two different functions never share a bare name. Where two traits each have a function of one name, a call is written with the trait's name, as [Traits](traits.md#when-two-traits-share-a-name) shows, and where two modules export one name, one is given another name or reached through its module, as [Importing with use](use.md#two-names-that-are-the-same) shows. A trait's function is still one function for every type that has an `impl`: `Add.add` of two `Rect`s and of two numbers are one function.

## The forms

| A name | Needs a `use`? |
| --- | --- |
| A type or trait that some syntax means | No |
| A function of such a trait, as `Add.add` | No, and it is written with the trait's name |
| `alloc`, `diverge`, and the other built-in effects | No |
| `push`, `wrapping_add`, `u32`, `sum`, `Map`, and every other name of `std` | Yes |
| `console`, `fs`, `net`, `time`, `ui`, and their operations, `print` among them | Yes |
| A name of a program's own module | Yes |
| A definition named as a prelude name is, or as a name the file imported | Refused: a name means one thing |

Next: [Importing a module](qualified-names.md)
