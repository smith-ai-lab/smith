---
name: smith-lang-defer
description: "defer in .smith modules: `defer mem::set_allocator(previous);` runs a statement when its block exits, last deferred first, on end of block, return, break, continue and goto out. Load when cleanup must run on every exit of a block (restore a handler, close a file, unlock, reset an arena), or on '`defer` is a keyword in a module'."
---

# defer

C has no way to run cleanup on every exit from a block. In a `.smith` file with a `module` line, `defer` is a keyword (C2y's defer technical specification, lowered to clang's `_Defer`; smith passes `-fdefer-ts` for every command with a `.smith` input):

```smith
module main;
import <stdio.h>;

static i32 first_word(i32 n) {
    defer c::puts("leave");           // runs when the function returns, whichever return
    if (n > 0) return 1;
    c::puts("not early");
    return 0;
}

i32 main() {
    {
        defer c::puts("runs last");
        defer { c::puts("runs first"); c::puts("a block works too"); }
        c::puts("body");
    }                                  // body, runs first..., runs last
    return first_word(1) - 1;
}
```

## Rules

- **Exactly the TS's meaning, no extension.** A deferred statement runs when its block exits, last deferred first: at the end of the block, `return`, `break`, `continue` and a `goto` out of it. What the TS forbids stays an error (jumping into a defer's scope, `return` or `goto` out of the deferred statement itself).
- **A keyword only in a module.** Without a `module` line `defer` is an ordinary identifier (`int defer = 1;` builds, as in C). In a module it names nothing: ``error: `defer` is a keyword in a module``. `x.defer` is a member and stays one.
- **A plain `.c` file** gets `_Defer` only when its own flags ask for `-fdefer-ts`; smith never changes a C default.
- `smith fmt` keeps `defer stmt;` and writes a `defer { ... }` block like any block. An affine value used in a deferred statement counts as used (`smith-lang-affine`).

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("defer").
