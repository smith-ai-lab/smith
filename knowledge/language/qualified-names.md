---
type: specification
title: Importing a module
description: A use with no list brings in the module itself under its last path segment, as gives it another name, and a name two modules share is written with its module.
status: planned
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
