---
type: specification
title: Entropy
description: The entropy effect, by which a program gets bytes nobody can predict from the operating system or the WebAssembly host, and how a handler gives a test fixed bytes.
status: partial
built:
  - "`std.entropy` declares the effect `entropy` with the operation `fill`, which a function that calls it lists and one that does not is refused with the error that names the effect, and which a program that reaches `main` with no handler has answered by the system on `linux_x64` and `linux_arm64`, with `getrandom` asked again until every byte is filled, and on `macos_x64` and `macos_arm64`, with `getentropy` asked for at most 64 bytes at a time until every byte is filled, and on `wasm`, where the import `entropy.fill` is answered by the loader of a JavaScript host with `crypto.getRandomValues` in pieces of at most 65536 bytes, and an executable that is refused says that the system gave no random bytes and ends with a failing status, so no byte that was not filled is used"
  - "a handler that answers `fill` gives the program inside it the bytes it gives, and a function that carries `entropy` runs under the handler in force where it is called"
  - "not built: the answer on `windows_x64` and `windows_arm64`, for each of which a program that asks for `entropy` is refused with an error that names the target, and a fresh `Key` of `std.random`"
---

# Entropy

The module `std.entropy` declares the `entropy` [effect](../language/effect-declarations.md). It is how a program gets bytes that nobody can predict, as a token or a challenge needs, and it is not the repeatable [`Key`](random.md) of `std.random`.

```
effect entropy {
  fn fill(bytes: take List[Byte]) -> List[Byte]
}
```

`fill` takes a list, fills every byte of it with bytes the system gives, and gives the list back. The list is the caller's and its size does not change, so a call needs no `alloc`.

```
use std.console (console, print)
use std.entropy (entropy, fill)
use std.fmt (hex)

fn main() ! console, entropy, alloc {
  bytes: List[Byte] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  var line = ""
  loop b in fill(bytes) { line = "{line}{hex(b, 2, false)}" }
  print(line)
}
```

This prints 64 hex digits, and two runs print different lines.

- A function that calls `fill` lists `entropy`, and one that does not is refused: "`fill` needs the `entropy` effect, which `draw` does not declare".
- With no handler, the operation is answered by the operating system: `getrandom` on Linux and `getentropy` on macOS, each asked again until the bytes are filled, and on WebAssembly the import `entropy.fill`, which the loader answers with `crypto.getRandomValues` in pieces of at most 65536 bytes.
- When the source fails, the program stops with an error that says the system gave no random bytes. It never goes on with bytes that were not filled.
- A handler answers `fill` like any operation, so a test can repeat a run. The program inside it gets exactly the bytes the handler gives.

```
use std.entropy (entropy, fill)
use std.list (push)

fn fixed[T](body: fn() -> T ! entropy) -> T ! alloc {
  entropy(
    fill: fn(bytes) {
      var made: List[Byte] = []
      loop i in 0..bytes.size { made = push(made, 171) }
      made
    },
    body: body,
  )
}
```

A secret, such as a token, is taken from `entropy` itself and not from a `Key`, which repeats when it starts from the same seed.
