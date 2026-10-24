---
name: smith-lang-effects
description: "Effects in .smith modules and C: what a function does to the world (alloc, files, net, console, clock, process, env, entropy, globals, c) is inferred from its body and its callees, [[smith::pure]] promises nothing at all, and a call that breaks the promise is a compile error that names the chain. Load when marking a function [[smith::pure]], or on 'smith-effect:' or 'is [[smith::pure]], but it has the effects'."
---

# Effects

A function does something to the world or it does not. smith works out which from the body and every function the body calls, and checks what you promise:

```smith
module stats;
import <stdlib.h>;
import <string.h>;

export [[smith::pure]] i32 length(const char *text) {   // strlen touches nothing: pure holds
    return (i32)c::strlen(text);
}

export void *grab(u32 size) {                           // no attribute: inferred (it allocates)
    return c::malloc(size);
}

i32 main() {
    return length("abc") - 3;
}
```

A function with no attribute is inferred and never an error. `[[smith::pure]]` says it does nothing at all; a call that brings in an effect is an error at the call, with the chain:

```text
stats.smith:9:5: error: smith-effect: `bad` is [[smith::pure]], but it has the effects console: bad -> log_it (stats.smith:9) -> printf (stats.smith:6)
```

## Rules

- **The effects**: `alloc` (malloc, free, mmap), `files`, `net`, `console`, `clock` (time, sleep), `process` (fork, exit, threads), `env` (getenv, uname), `entropy` (arc4random), `globals` (writing a global or a function-local `static`) and `c` (unchecked C). Allocation is an effect. **Crashing is not**: `abort`, a trap, a failed contract and a slice index out of range, so a function that indexes a slice can be pure.
- **What a body reaches**: the calls of the file and the headers it includes, followed to a function of libc or libSystem (a table of smith's gives each its effects) or to a call the file cannot see into: a call through a function pointer, inline assembly and an Objective-C message are `c`, and so is a function declared with no body.
- **The promise is what a caller sees**, not the inferred set. Recursion is no effect here.
- **Where it is checked**: in the compile of a file, whenever a function carries the attribute (in a module, in a `.smith` file with no `module` line and in C), and in the editor.
- **Not yet** (issue #207): a ceiling that allows some effects, `[[smith::effects(alloc, net)]]`, and the same on a declaration with no source as a promise; effects across files (a call into another file is `c`); recursion and stack size; hover; `effects::within`.

Overview: `smith-lang`. Reference: [knowledge/effects-check.md](../../../knowledge/effects-check.md).
