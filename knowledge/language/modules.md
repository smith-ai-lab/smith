---
type: specification
title: Modules
description: One file as one module, pub, and explicit imports with use.
status: planned
---

# Modules

One file is one module, named by its path, so `db/transaction.smith` is the module `db.transaction`. Everything in a module is private unless marked `pub`, on an item or on a single field.

```
pub struct Rect {
  pub w: f32
  pub h: f32
}

pub struct Transaction {
  id: u64
}

pub fn begin(db: Db) -> Transaction ! net
pub fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn next_id(db: Db) -> u64 ! net
```

- A struct can be built or taken apart only by code that can see all of its fields, and a field can be assigned only by code that can see it. Any module can build a `Rect`. Other modules can hold a `Transaction` and pass it on, but cannot forge one or dispose of one, which is what makes `without` and proof fields trustworthy.
- A private definition can be used only inside its own module.

Another module's names are brought in by an explicit list. There is no way to import everything.

```
use db.transaction (Transaction, begin, commit)

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

- A small prelude is in scope everywhere: `Option`, `Result`, the `Try`, `Fmt`, and `Repr` traits, the numeric types and `bool`, the operator traits with `Eq` and `Ord`, the effects `console`, `fs`, `net`, `time`, and `ui`, `print`, and the array functions such as `sum` and `map`.
- When two modules export the same name, the modules are imported and the name is qualified, as in `transaction.begin(db)`.
- The formatter sorts the list. A missing import is an error that names the module to import from, and an unused one is a warning from `smith check`.
- An `impl` is not imported. It is visible everywhere, and it must live in the module that defines the trait or the type, which keeps one implementation per type checkable.

A `use` with no list brings in the module itself, under its last path segment, and `as` gives it another name when two last segments clash. All `use` lines sit at the top of the file, one per module, and nothing is re-exported.

```
use db.transaction
use ui.animation as anim

t = transaction.begin(db)
a = anim.begin(view)
```

Next: [Projects and packages](projects.md)
