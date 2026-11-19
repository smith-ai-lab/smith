---
type: specification
title: Values used exactly once
description: A value with neither Copy nor Discard must be taken exactly once on every path, which is how a resource such as a file or a transaction is checked.
status: partial
built:
  - "a value of a type without `Discard`, held by a parameter marked `take`, a binding, or a `var`, is refused where a path drops it: at the end of the function, on one branch of an `if` or a `match`, before a `return` or a `try` that leaves, in a loop with `break`, and when a `var` is assigned again"
  - "a struct, a tuple, an enum, and a list that holds a value without `Discard` has no `Discard`, and a value of one is taken apart or given on as any other is"
  - "reading a value, lending it, and reading a field of it take nothing, and a value given away twice is refused at the second use"
  - "a part of a value that is taken leaves each other part without `Discard` to be taken, on every path, for a struct, one in a struct, and a tuple taken apart"
  - "a value without `Discard` that is made and only lent or read, as an argument of a parameter that lends, a field read from it, or a statement of its own, is refused as dropped at the value, whether it is of a type of the program's or a type parameter's"
  - "a parameter of a lambda that is taken is followed as a parameter of a function is, whether its type is written or not, and a lambda that drops it is refused at the parameter"
  - "a lambda that gives away a value without `Discard` is refused where its function is lent or may be left uncalled, with one error at the lambda"
---

# Values used exactly once

A value whose type has neither `Copy` nor `Discard` must be taken exactly once on every path. That is how a resource is checked: it cannot be forgotten, and it cannot be used again once it has been given up.

```
struct Transaction {
  id: u64
}

without Copy for Transaction
without Discard for Transaction

fn begin(db: Db) -> Transaction ! net
fn debit(t: Transaction, amount: u64) ! net
fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  debit(t, 100)
  commit(t)
}
```

`debit` lends `t`, so `t` is still there for `commit`, which takes it.

## What is refused

Leaving out `commit(t)` does not compile, as `t` is dropped and a `Transaction` has no `Discard`.

```
fn forgotten(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  debit(t, 100)
  .ok(())
}
```

Calling it twice does not compile, as `t` has no `Copy` and the first call took it.

```
fn twice(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  try commit(t)
  commit(t)
}
```

## Every path

On every path means each branch takes `t` once. This is accepted.

```
fn rollback(t: take Transaction) -> Result[(), DbError] ! net

fn finish(db: Db, ok: bool) -> Result[(), DbError] ! net {
  t = begin(db)
  if ok { commit(t) } else { rollback(t) }
}
```

Taking it on one branch only is refused, as the other branch drops it.

```
fn partial(db: Db, ok: bool) -> Result[(), DbError] ! net {
  t = begin(db)
  if ok { commit(t) } else { .ok(()) }
}
```

A struct, a tuple, an enum, and a list that holds a `Transaction` has neither trait either, so a value of one is taken exactly once, and a value is given on whole, or taken apart and each part taken. Taking one part of a struct leaves each other part that has no `Discard` to be taken, on every path. A value that is made and nothing takes, as `peek(begin())` lends it and `begin().id` reads a field of it, is dropped too, and is refused at the value.

## Leaving and assigning

A `try` that leaves, a `return`, and a `break` each end a path, and the value must be taken on it as on any other. This is refused, as `try` leaves when `prepare` fails and `t` is dropped there.

```
fn prepare(db: Db) -> Result[(), DbError] ! net

fn early(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  try prepare(db)
  commit(t)
}
```

A `var` that holds such a value is taken before it is assigned again, as the assignment drops the value it held, and before it goes out of scope.

```
fn replaced(db: Db) -> Result[(), DbError] ! net {
  var t = begin(db)
  debit(t, 100)
  t = begin(db)
  commit(t)
}
```

This is refused at the first `t`, which the assignment drops.

## A resource wrapper

`open` below is written for [`with`](with.md): it lends the handle to the rest of the caller's block, and closes it afterwards. Leaving out `close_file`, or putting a step that can fail between opening and closing, does not compile. The body's own effects, here `alloc` from `read`, are allowed because `first_bytes` declares them.

```
fn open[T](path: str, body: fn(File) -> Result[T, FsError]) -> Result[T, FsError] ! fs {
  handle = try open_file(path)
  r = body(handle)
  close_file(handle)
  r
}

fn first_bytes(path: str) -> Result[List[u8], FsError] ! fs, alloc {
  with f = open(path)
  read(f, 0..16)
}
```

Most code only ever writes the `with` line. Generic functions state `T: Copy` where they copy, as [Ownership](ownership.md) shows, and tooling proposes the bound.

## The forms

| A value of a type with | May be | Must be |
| --- | --- | --- |
| `Copy` and `Discard` | Duplicated, dropped | Nothing |
| `Discard` only | Dropped | Taken at most once |
| Neither | Neither | Taken exactly once on every path |

Next: [Arrays and sizes](arrays.md)
