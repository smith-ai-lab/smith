---
type: specification
title: Modules
description: A file as a module, and how its path gives the module its name.
status: built
---

# Modules

A module is one file, and its name is its path. The file `db/transaction.smith` is the module `db.transaction`.

```
db/
  transaction.smith
```

```
struct Transaction {
  id: u64
}

fn begin(db: Db) -> Transaction ! net
```

These lines, written in `db/transaction.smith`, are the module `db.transaction`. It holds a struct and a function. Another module reaches them by that name, as [Importing with use](use.md) describes.

## The name of a module

The name is the path with each `/` written as `.` and `.smith` left off, counted from the project's root, as [Projects](projects.md) describes. A folder adds one segment to the name of every module below it. No file is named another way: the entry file `main.smith` is the module `main`.

| Path | Module |
| --- | --- |
| `main.smith` | `main` |
| `db/transaction.smith` | `db.transaction` |
| `model/gpt.smith` | `model.gpt` |
| `vendor/toml/parse.smith` | `vendor.toml.parse` |

A module's definitions can be used by other modules only when they are marked `pub`, as [Public and private](pub.md) describes.

Next: [Public and private](pub.md)
