---
type: specification
title: The prelude
description: The names that are in scope in every file without a use, and what a file imports for everything else.
status: partial
built:
  - "the names below, in scope in every file"
  - "not `use`, so every other name of the standard library that the compiler builds, such as `print`, `console`, `push`, `narrow`, and the `wrapping_` functions, is in scope too until it is, and the examples of the other pages are written without their `use` lines until then"
---

# The prelude

A small set of names is in scope in every file with no `use`. It is called the prelude, and it is as small as it can be: a name is in it only when the syntax of the language means that name. There is no other way in, and so no name is in it for convenience.

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
| `i8` to `i64`, `u8` to `u64`, the float types, `bool`, `str`, `Nat` | A literal, and the size of an array |
| `List` | A list written in brackets, as `[1, 2, 3]` |
| `Option`, `Result`, and `Try` with its `then` | `try` |
| `Order`, `Eq`, and `Ord`, with `eq` and `compare` | `==`, `!=`, `<`, `<=`, `>`, and `>=` |
| `Add`, `Sub`, `Mul`, `Div`, `Rem`, `Pow`, and `Neg`, with their functions | The arithmetic operators |
| `Index`, with `valid` and `at` | `x[i]` |
| `Iterate` with `each`, and `Flow` | `loop v in x`, `break`, and `return` in one |
| `Exit` with `exit` | What `main` returns |
| `Fmt` and `Repr`, with their functions | An expression in a string's braces |
| `Convert` with `convert` | Two types that meet in an operator, and a failure that `try` converts |
| `Hash`, `Copy`, and `Discard` | The traits a type is given with no `impl` |
| `FromInt`, `FromFloat`, `FromString`, and `FromArray` | The literals |
| `alloc`, `diverge`, and the other effects the compiler gives | A string that holds an expression, an open-ended `loop`, and the rest of [the built-in effects](provided-effects.md#built-in) |

## Everything else is imported

A name that is not in the table is imported. That holds for the rest of the standard library as for a program's own modules.

```
use std.list (push)
use std.fs (fs, read_all, FsError)

fn lines_of(path: str) -> Result[List[u8], FsError] ! fs, alloc {
  read_all(path)
}
```

`push` grows a list, and no syntax means it, so it is imported. So are `narrow` and the `wrapping_`, `saturating_`, and `checked_` functions, the functions of `Bits`, `utf8`, `Map`, and the array functions such as `sum` and `map`. An effect the standard library declares is imported as any effect is, by its name, with each operation the file calls: `fs` and `read_all` above, and `console` and `print` in the first program.

A name used and not imported is refused, and the error names the module to import it from, as [Importing with use](use.md#a-name-that-is-missing) says.

## The forms

| A name | Needs a `use`? |
| --- | --- |
| A type, trait, or function that some syntax means | No |
| `alloc`, `diverge`, and the other built-in effects | No |
| `push`, `narrow`, `sum`, `Map`, and every other name of `std` | Yes |
| `console`, `fs`, `net`, `time`, `ui`, and their operations, `print` among them | Yes |
| A name of a program's own module | Yes |

Next: [Importing a module](qualified-names.md)
