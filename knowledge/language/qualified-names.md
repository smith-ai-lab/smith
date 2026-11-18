---
type: specification
title: Importing a module
description: A use with no list brings in the module itself under its last path segment, as gives it another name, and a name two modules share is written with its module.
status: partial
built:
  - "`use a.b`, which brings in the module of a project or of the library itself under its last segment, and `use a.b as c`, which brings it in as `c`, so that `b.name(x)` or `c.name(x)` calls a name of the module that the module writes with `pub`, a function or the construction of a struct, with the module's own names, as in the file that defines them, and an `impl` where it was; nothing is added to the executable by writing the module"
  - "an error at its place for a module whose last segment is the last segment of another imported without `as`, for a name of a module that the file defines, lists, or the prelude holds, for a name written with a module that is not imported, and for a name the module keeps private or does not have; and a warning from `smith check` for a module imported and never written before a name"
  - "not a type of a module written with the module, as in `transaction.Transaction`, nor a name the compiler builds written with its module, as in `list.push(xs, 1)`; nor `as` after a module together with a list"
---

# Importing a module

A `use` with no list brings in the module itself, under its last path segment. Its names are written with the module's name before them.

```
use db.transaction

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = transaction.begin(db)
  transaction.commit(t)
}
```

`db.transaction` is imported as `transaction`, its last segment, so `begin` is written `transaction.begin`.

## A name that two modules share

When two modules export the same name and a file uses both, the name cannot be listed from both, as [Importing with use](use.md#two-names-that-are-the-same) says. The modules are imported and the name is written with its module.

```
use db.transaction
use ui.animation

t = transaction.begin(db)
a = animation.begin(view)
```

Both modules export `begin`. Each call says which module's it is. Where the file uses only that one name of a module, giving the name another name in the list, as `use ui.animation (begin as start)`, says the same with less.

## A different name for a module

`as` gives the module another name. It is written when two last segments clash.

```
use db.transaction
use ui.animation as anim

t = transaction.begin(db)
a = anim.begin(view)
```

Here `ui.animation` is imported as `anim`, and the other is still `transaction`.

## The same rules as a list

All `use` lines sit at the top of the file, one per module, and nothing is re-exported, as [Importing with use](use.md) describes.

## The forms

| Written | Means |
| --- | --- |
| `use m (a, b)` | The names `a` and `b` of `m` are in this file |
| `use m (a as c)` | The name `a` of `m` is in this file as `c` |
| `use a.b` | The module itself, as `b`, so `b.name` is a name of it |
| `use a.b as c` | The module itself, as `c`, so `c.name` is a name of it |

Next: [Where an impl lives](impl-placement.md)
