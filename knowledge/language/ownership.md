---
type: specification
title: Ownership
description: The Copy and Discard traits, lending and take, and values that must be used at most once or exactly once.
status: partial
built: "`without`, for `Fmt`, `Repr`, and `Eq`"
---

# Ownership

Two traits say what may happen to a value. `Copy` means it may be duplicated, and `Discard` means it may be dropped unused. Both are derived automatically: a type has each trait unless one of its fields lacks it.

| Kind | Traits | Examples |
| --- | --- | --- |
| Ordinary | `Copy` and `Discard` | Numbers, arrays, most structs |
| Used at most once | `Discard` only | A random `Key` |
| Used exactly once | Neither | A raw file handle, a transaction |

Passing an argument lends it: the callee cannot keep, store, or return it. A parameter marked `take` receives ownership, and the caller cannot use the value again. For `Copy` types the difference is invisible.

```
fn split(k: take Key) -> (Key, Key)
fn uniform(k: take Key) -> f32

fn two_numbers(key: take Key) -> (f32, f32) {
  (k1, k2) = split(key)
  (uniform(k1), uniform(k2))
}
```

Using `k1` twice would be a compile error, so a random key cannot be reused. Randomness needs no effect, because every random function takes a key.

A type opts out of a trait it would get automatically with a `without` statement, which works for any automatically derived trait. Its fields stay plain.

```
struct Transaction {
  id: u64
}

without Copy for Transaction
without Discard for Transaction

fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  debit(t, 100)
  commit(t)
}
```

`debit` borrows `t` and `commit` takes it. Leaving out `commit(t)`, or calling it twice, does not compile.

A value with neither trait must be taken exactly once on every path. That is how a resource wrapper is checked. `open` below is written for `with`: it lends the handle to the rest of the caller's block and closes it afterwards. Leaving out `close_file`, or putting a step that can fail between opening and closing, does not compile. The body's own effects, here `alloc` from `read`, are allowed because `first_bytes` declares them.

```
fn open[T](path: str, body: (File) -> Result[T, FsError]) -> Result[T, FsError] ! fs {
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

Most code only ever writes the `with` line. Generic functions state `T: Copy` where they copy, and tooling proposes the bound.

Next: [Arrays and sizes](arrays.md)
