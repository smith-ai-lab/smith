---
type: specification
title: Public and private
description: Everything in a module is private unless marked pub, on an item or on a single field, and what that lets another module do with a struct.
status: partial
built:
  - "`pub` before a `fn`, `struct`, `enum`, or `trait` of a module of a project, which is then the name another module lists in a `use`, and the functions of a `pub` trait, which are listed by their own names"
  - "a name that is not `pub` refused in a `use` with its own error, and a name written with no `use` refused as any name that is not there"
  - "`pub` before a field of a struct of a module of a project or of the library, and a field without it refused, at its place, in another module when it is read, assigned, or built by a call of the struct"
  - "not `pub` before an `effect`, or in the file that is built"
---

# Public and private

Everything in a module is private unless it is marked `pub`. A private definition can be used only inside its own module. A `pub` definition can be used by other modules, which import it as [Importing with use](use.md) describes.

```
pub fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn next_id(db: Db) -> u64 ! net
```

Written in `db/transaction.smith`, `commit` can be imported by any module and `next_id` cannot. Code in the same file may use both.

```
pub fn begin(db: Db) -> Transaction ! net {
  Transaction(id: next_id(db))
}
```

## A private definition used from another module

Another module that names `next_id` is refused, since `next_id` is private.

```
use db.transaction (next_id)
use std.bits (u64)

fn fresh(db: Db) -> u64 ! net {
  next_id(db)
}
```

## What `pub` is written on

`pub` is written before the item it opens up: `pub fn`, `pub struct`, and `pub effect`, as in [Declaring an effect](effect-declarations.md).

```
pub struct Rect {
  pub w: f32
  pub h: f32
}
```

`pub` is also written on a single field of a struct, as `w` and `h` are above. A field with no `pub` is private, whatever the struct is.

```
pub struct Transaction {
  id: u64
}
```

`Transaction` can be imported, and its field `id` is private.

## Building and changing a struct from another module

A struct can be built or taken apart only by code that can see all of its fields, and a field can be assigned only by code that can see it.

Every field of `Rect` is `pub`, so any module can build one and assign its fields.

```
fn wider(r: Rect) -> Rect {
  var wide = Rect(w: 1.0, h: r.h)
  wide.w = r.w * 2.0
  wide
}
```

`Transaction` has a private field, so another module can hold a `Transaction` and pass it on, but cannot forge one or dispose of one.

```
fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

`transfer` holds a `Transaction` that `begin` gave it, and passes it to `commit`. Building one is refused, since `id` is private.

```
fn forged() -> Transaction {
  Transaction(id: 7)
}
```

Assigning its field is refused for the same reason.

```
fn renumbered(db: Db) -> Transaction ! net {
  var t = begin(db)
  t.id = 8
  t
}
```

This is what makes [`without`](without.md) and [proof fields](proof-fields.md) trustworthy. A `Transaction` is a value that must be taken exactly once, as [Values used exactly once](resources.md) describes, and a module that does not own it can neither make one nor dispose of one.

## The forms

| Written | Means |
| --- | --- |
| `fn f` | Private: used only inside its module |
| `pub fn f` | Public: any module may import it |
| `pub struct S` | The struct is public, and each field is private unless it has `pub` |
| `pub w: f32` | A public field: code outside the module can see it |
| `w: f32` | A private field: code outside the module cannot build, take apart, or assign the struct |

Next: [Importing with use](use.md)
