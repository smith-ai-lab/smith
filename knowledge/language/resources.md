---
type: specification
title: Values used exactly once
description: A value with neither Copy nor Discard must be taken exactly once on every path, which is how a resource such as a file or a transaction is checked.
status: planned
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
