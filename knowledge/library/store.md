---
type: specification
title: Stored values
description: The store effect, which keeps bytes under keys past the end of the program, what it promises, save and load, which keep any value that has Serialize, and where the bytes are kept.
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
- `failed` is a store that could not do what was asked, as a disk that is full, with what it said. `unreadable` is what `load` gives when the bytes under a key are not the type it reads, with the words of the [`ReadError`](deserialize.md).

An operation takes bytes and no value with `Serialize`, because an operation is where the program meets what is outside it, and what is outside has no types: in a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) each operation is an import. Turning a value into bytes is the library's work, in `save`, and not the work of what answers the effect.

## What it promises

Whatever answers `store` keeps two promises, and one that cannot is not allowed to answer it.

- **A read sees every write before it.** `get` after a `put` of the same key gives those bytes, in this run and in every later one, from wherever the program runs.
- **The writes of one request are kept together, or none is.** What a program writes while it answers one request of [`http`](http.md), or one message of [`mcp`](mcp.md), is all there when the answer is given, and when the program stops before that, by an error or by the machine, none of it is.

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
| `get(key)`, `put(key, bytes)`, `delete(key)`, `keys(prefix)` | The operations of the effect, on bytes |
