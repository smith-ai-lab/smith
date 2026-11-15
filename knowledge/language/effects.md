---
type: specification
title: Effects
description: Effects listed in a signature as permissions, the built-in effects, and handlers.
---

# Effects

A function with no `!` clause is pure. Effects are listed after the return type, and a caller must have every effect its callees use. A function without an effect cannot perform it, so effects work as permissions. Below, `load` is shown by its signature alone.

```
fn load(path: str) -> Result[Bytes, IoError] ! fs, alloc

fn fixture_config() -> Result[Config, IoError] ! alloc {
  with memory_fs(fixtures)
  bytes = try load("model.toml")
  parse_config(bytes)
}
```

- The built-in effects are `console`, `fs`, `net`, `time`, `ui`, `gpu`, `foreign`, `alloc`, `spawn`, `exec` for running another program, and `diverge` for code that may not terminate.
- A handler is a function that runs a lambda with an effect handled, installed with `with`. It is declared by putting the effect on its lambda parameter, as in `fn memory_fs[T](files: Files, body: () -> T ! fs) -> T`. `memory_fs` above handles `fs` with an in-memory file system, so `fixture_config` does not carry `fs`.
- The stored signature always states the effect list. The compiler proposes it while the function is being written, and lambdas are inferred.
- A lambda's other effects are checked where it is written: whatever its body does must be allowed by the function it sits in, and the function receiving it says nothing about them. This is sound because a lambda passed to a call cannot be stored or returned. There are no effect variables.
- A function stored in a value states its effects in its type, and is pure when none are written.

Next: [Text and printing](text.md)
