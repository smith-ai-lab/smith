---
type: specification
title: Stored values
description: The store effect, which keeps bytes under keys past the end of the program, what it promises, together, which keeps the writes of a block together, save and load, which keep any value that has Serialize, and where the bytes are kept.
status: planned
---

# Stored values

The module `std.store` declares the `store` [effect](../language/effect-declarations.md), by which a program keeps values that are still there when it runs again. A program says what it keeps and under which key. Where the bytes are kept is the deployment's to say, as where requests come from is for [`http`](http.md).

Most programs keep a value of a type of their own, with `save` and `load`.

```
use std.deserialize (ReadError)
use std.store (store, load, save, StoreError)

struct User {
  name: str
  visits: Nat
}

fn remember(id: str, user: User) -> Result[(), StoreError] ! store, alloc {
  save(id, user)
}

fn recall(id: str) -> Result[Option[User], StoreError] ! store, alloc {
  load(id)
}
```

After `remember("user/7", User(name: "Ada", visits: 3))`, `recall("user/7")` is `.ok(.some(User(name: "Ada", visits: 3)))`, in this run of the program and in the next. `recall("user/8")` is `.ok(.none)`, as nothing was kept under that key.

```
fn save[T: Serialize](key: str, value: T) -> Result[(), StoreError] ! store, alloc
fn load[T: Deserialize](key: str) -> Result[Option[T], StoreError] ! store, alloc
```

Both are written in Smith with the operations below. `save` keeps the bytes of [`to_json`](json.md) of the value, and `load` reads them with `from_json` as the type the place of its result needs. So what is kept is in one format that the program chose, whatever answers the effect, and a value kept under one deployment is read under another.

## The effect

The effect moves bytes, and knows no type.

```
effect store {
  fn get(key: str) -> Result[Option[Slice[Byte]], StoreError]
  fn put(key: str, value: Slice[Byte]) -> Result[(), StoreError]
  fn delete(key: str) -> Result[(), StoreError]
  fn keys(prefix: str) -> Result[List[str], StoreError]
  fn begin()
  fn keep() -> Result[(), StoreError]
  fn drop_writes()
}

enum StoreError {
  unreadable: str
  failed: str
}
```

- `get` gives the bytes kept under the key, or `.none` when there are none.
- `put` keeps the bytes under the key, in place of what was there.
- `delete` removes what is under the key, and does nothing when nothing is.
- `keys` gives every key that begins with the prefix, in the order of their bytes. With `""` it gives every key.
- `begin`, `keep`, and `drop_writes` open a group of writes, keep it, and drop it. A program does not call them: [`together`](#writes-that-are-kept-together) does.
- `failed` is a store that could not do what was asked, as a disk that is full, with what it said. `unreadable` is what `load` gives when the bytes under a key are not the type it reads, with the words of the [`ReadError`](deserialize.md).

An operation takes bytes and no value with `Serialize`, because an operation is where the program meets what is outside it, and what is outside has no types: in a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) each operation is an import. Turning a value into bytes is the library's work, in `save`, and not the work of what answers the effect.

## What it promises

Whatever answers `store` keeps two promises, and one that cannot is not allowed to answer it.

- **A read sees every write before it.** `get` after a `put` of the same key gives those bytes, in this run and in every later one, from wherever the program runs.
- **The writes of one group are kept together, or none is.** What a program writes inside [`together`](#writes-that-are-kept-together) is all there when the block ends well, and when it ends with an error, or the machine stops before it ends, none of it is.

## Writes that are kept together

`together` groups the writes of a block. It is written with [`with`](../language/with.md).

```
use std.int (clamp)
use std.store (StoreError, load, save, store, together)

fn move_points(from: str, to: str, n: Nat) -> Result[(), StoreError] ! store, alloc {
  with together()
  a: Option[Nat] = try load(from)
  b: Option[Nat] = try load(to)
  match (a, b) {
    (.some(x), .some(y)) {
      if x >= n {
        left: Nat = x - n
        more: Nat = clamp(y + n)
        try save(from, left)
        save(to, more)
      } else {
        .err(.failed("too few points"))
      }
    }
    _ { .err(.failed("no such account")) }
  }
}
```

Either both accounts change or neither does. When the block gives `.ok`, both writes are kept. When it gives `.err`, at the first `try` or at its end, nothing it wrote is kept. No reader, in this program or another, ever sees one write without the other.

```
fn together[T](body: fn() -> Result[T, StoreError] ! store) -> Result[T, StoreError] ! store
```

- A read inside the block sees what the block has written.
- A block gives a `Result`, and its error decides: `.err` drops every write of the block. A store that cannot keep the writes when the block ends is `.err(.failed(...))`, and then none is kept.
- A `together` inside another is part of the outer one: its writes are kept when the outer block ends well, and dropped when either gives `.err`.
- **A request is a group.** [`routes`](http.md) and [`mcp`](mcp.md) run each request and each message inside `together`, and treat a response that is an error as `.err`. So a server writes no `together` line to have the writes of one request kept together, and a program that answers no request, as a command of a project, writes one where it needs it.
- Outside any group, each write is kept alone, when its call returns.

## Where the bytes are kept

What answers `store` is the deployment and not the program.

- A test answers it with a handler that keeps a [`Map`](map.md) in memory, so a program that keeps values is tested with no disk.
- A native executable answers it with a handler written in Smith over [`fs`](fs.md).
- In a WebAssembly module its operations are imports, and the host supplies a store that keeps both promises. A store whose reads may be behind its writes keeps neither, so it does not answer `store`.

## The forms

| Written | Is |
| --- | --- |
| `save(key, value)` | The value kept under the key, as the bytes of its JSON |
| `load(key)` | `.some` of the value kept under the key, read as the type expected, or `.none` |
| `with together()` | The writes of the rest of the block, kept together or not at all |
| `get(key)`, `put(key, bytes)`, `delete(key)`, `keys(prefix)` | The operations of the effect, on bytes |
