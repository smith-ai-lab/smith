---
type: specification
title: Importing with use
description: use brings the names of another module into a file, from an explicit list, and what is said when a name is missing or unused.
status: planned
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

What the standard library puts in scope everywhere, such as `print` and the five effects, is unchanged, as [The prelude](prelude.md) describes.

## The formatter sorts the list

The names in a list are written in the order the formatter gives them, which is sorted. `use db.transaction (begin, commit, Transaction)` is rewritten by the formatter as `use db.transaction (Transaction, begin, commit)`.

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
| `use app.log (log)`, then a call of `write` | Refused: an effect's operations are imported by name too |
| A name never imported | An error that names the module to import from |
| A name imported, never used | A warning from `smith check` |
| `use` below code | Not written: every `use` is at the top of the file |

Next: [The prelude](prelude.md)
