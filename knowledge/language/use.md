---
type: specification
title: Importing with use
description: use brings the names of another module into a file, from an explicit list, and what is said when a name is missing or unused.
status: partial
built:
  - "`use m (a, b)` for a module of the standard library, written `std.console`, `std.list`, `std.utf8`, `std.int`, or `std.bits`, listing names that module exports, and a `use` before every other item with one line for a module"
  - "an error at its place, with a fix where there is one, for a module that does not exist, a name the module lacks, a name listed twice, a name listed from two modules, a name the file defines or the prelude holds, a second `use` of one module, and a `use` below another item"
  - "a warning from `smith check`, at the name, for a name that is imported and never used"
  - "only the modules a `use` names are read, and the same executable is built with and without an import"
  - "`use m (a, b)` for a module of a project, written `db.transaction` for the file `db/transaction.smith` below the folder the command ran in, listing the names the module writes with `pub`, which the module may itself take from others with its own `use`, each file read once and only when a `use` names it"
  - "an error at its place for a `use` of a file that does not exist, of a name the module keeps private, of a name it does not define, and of modules that import each other in a ring no order builds, and for a `use` of the file that is built"
  - "a file that no `use` reaches is never read by `smith build` or `smith run`, and `smith check` with no file named reads it and says what its syntax and its `use` lines say"
  - "`use m (a as c)`, which gives the name `a` of the module `m` the name `c` in this file only, so that two modules' `begin` are both used, and a function of a trait or of the library that the compiler builds is not given another name; an error at its place for a `c` that the file defines, that the prelude holds, that another `use` lists, or that is the name of an imported module, and nothing in the executable says `c`"
  - "a function of a trait that two traits the file can name both have, called with no trait written before it, is one error at the call, which says to write `Trait.function`; `Trait.function(x)` is the call of that trait's function, for a trait the file defines or lists"
  - "an `impl` of a trait that a file lists with `use`, whose functions the file does not list: a function written in the `impl` is the function of that trait of the same name, so two traits of two modules that each have `show` are both implemented in one file, and an `impl` that lacks a function of the trait or writes one it does not have is one error at its place that names the function as the module wrote it"
  - "not a name used and not imported from the standard library, which is still in scope as it was, so the examples of the other pages are still written without their `use` lines; nor `pub` in the file that is built"
  - "a function, struct, enum, trait, or `impl` of a module of the standard library whose source is Smith is compiled when a program reaches it, as one of the program's own file is, so the library can be written in Smith; the library's names that the compiler builds, as [the compiler's design](../compiler.md#bootstrap) says, are still supplied by it, because effect declarations are not built"
---

# Importing with use

A module uses another module's names by listing them in a `use`: the module's name, then the names in parentheses.

```
use db.transaction (Transaction, begin, commit)

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

`Transaction`, `begin`, and `commit` are now names in this file, as if it had defined them. Only a `pub` name can be listed, as [Public and private](pub.md) describes.

## Every name is listed

Another module's names are brought in by an explicit list. There is no way to import everything. A file lists each name it takes.

```
use db.transaction (Transaction, begin, commit)
use model.tokenizer (decode, encode)
```

## Nothing is imported implicitly

Every name a file uses from another module is named in its `use` line. Importing an effect brings in the effect's name and nothing else: each operation a file calls is imported by its own name.

```
use app.log (log, write)

fn greet() ! log {
  write("hello")
}
```

`log` is the effect and `write` is its operation, so both are listed. Listing only the effect is not enough: calling `write` after `use app.log (log)` is refused.

```
use app.log (log)

fn greet() ! log {
  write("hello")
}
```

A trait's function is imported by its own name in the same way. `area` is a function of the trait `Area`, so a file that calls it lists it with the trait and the type.

```
use shapes (Area, Rect, area)

fn size(r: Rect) -> i32 {
  area(r)
}
```

An `impl` of a trait needs the trait listed and not its functions. A function written inside `impl Shown[Mine]` is the function `show` of `Shown`, whether or not the file lists `show`. A `use` lists a trait's function only so that a call may be written without the trait before it.

```
use ui.animation (Shown)

struct Mine {
  n: i32
}

impl Shown[Mine] {
  fn show(x: Mine) -> str { "shown" }
}

fn main() ! console, alloc {
  print(Shown.show(Mine(n: 1)))
}
```

So a file may implement `Shown` of `ui.animation` and `Drawn` of `db.render` for its own types when both traits have a function `show`, and calls each as `Shown.show(x)` and `Drawn.show(x)`.

The few names that need no `use` are those the syntax itself means, which [The prelude](prelude.md) lists. `print` is not one of them.

## Two names that are the same

A name in a file means one thing, so a name cannot be listed from two modules. Here both modules export `begin`.

```
use db.transaction (begin)
use ui.animation (begin)
```

This is refused at the second `use`. Modules are not built, so no words are quoted.

`as` after a name in the list gives it another name in this file, and then both can be listed.

```
use db.transaction (begin)
use ui.animation (begin as start)

fn open(db: Db, view: View) ! net, ui {
  t = begin(db)
  a = start(view)
}
```

`start` is `begin` of `ui.animation`, and `begin` is that of `db.transaction`. The other name holds in this file only: the module still exports `begin`, and another file lists it as `begin`.

`as` on a name is written where a file uses that one name of the module. Where it uses several of the module's names that clash, the module is imported and the names are written with it, as [Importing a module](qualified-names.md) shows.

## The formatter sorts the list

The names in a list are written in the order the formatter gives them, which is sorted by the name the module gives, so `begin as start` sorts as `begin`. `use db.transaction (begin, commit, Transaction)` is rewritten by the formatter as `use db.transaction (Transaction, begin, commit)`.

## A name that is missing

A name used and not imported is an error, and the error names the module to import it from.

```
fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

Here nothing imports `begin` or `commit`, so the function is refused, and the error names `db.transaction`.

## A name that is unused

A name that is imported and never used is a warning from `smith check`, not an error.

```
use db.transaction (Transaction, begin, commit, rollback)
```

If `rollback` is never used in the file, `smith check` warns about it.

## Where `use` is written

All `use` lines sit at the top of the file, one per module. Two names from one module are listed in one `use`, not written as two.

```
use db.transaction (Transaction, begin, commit)
use model.tokenizer (decode, encode)

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

Nothing is re-exported. A name a file imports is for that file alone, and a module that imports `begin` does not offer `begin` to the modules that import it.

## The forms

| Written | Means |
| --- | --- |
| `use m (a, b)` | The names `a` and `b` of the module `m` are in this file |
| `use m (b, a)` | The same, which the formatter writes as `use m (a, b)` |
| `use m (a as c)` | The name `a` of `m` is in this file as `c`, which ends a clash with another `a` |
| `use m (a)` and `use n (a)` | Refused: a name in a file means one thing |
| `use app.log (log)`, then a call of `write` | Refused: an effect's operations are imported by name too |
| A name never imported | An error that names the module to import from |
| A name imported, never used | A warning from `smith check` |
| `use` below code | Not written: every `use` is at the top of the file |

Next: [The prelude](prelude.md)
