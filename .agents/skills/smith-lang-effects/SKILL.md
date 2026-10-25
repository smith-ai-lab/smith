---
name: smith-lang-effects
description: "Effects in .smith modules and C: what a function does to the world (alloc, files, net, console, clock, process, env, entropy, globals, c) is inferred from its body and its callees, [[smith::pure]] promises nothing at all, [[smith::effects(alloc, net)]] is a ceiling, and a call that breaks the promise is a compile error that names the chain. Load when marking a function [[smith::pure]] or [[smith::effects(...)]], or on 'smith-effect:', 'is [[smith::pure]], but it has the effects' or 'is limited to'."
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

export [[smith::effects(alloc)]] void *grab(u32 size) { // may allocate, and nothing else
    return c::malloc(size);
}

export void *plain(u32 size) {                          // no attribute: inferred (it allocates)
    return c::malloc(size);
}

i32 main() {
    return length("abc") - 3;
}
```

A function with no attribute is inferred and never an error. `[[smith::pure]]` says it does nothing at all, `[[smith::effects(alloc, net)]]` that it may do those and nothing else; a call that brings in an effect past the promise is an error at the call, with the chain:

```text
stats.smith:9:5: error: smith-effect: `bad` is [[smith::pure]], but it has the effects console: bad -> log_it (stats.smith:9) -> printf (stats.smith:6)
```

## Rules

- **The effects**: `alloc` (malloc, free, mmap), `files`, `net`, `console`, `clock` (time, sleep), `process` (fork, exit, threads), `env` (getenv, uname), `entropy` (arc4random), `globals` (writing a global or a function-local `static`) and `c` (unchecked C). Allocation is an effect. **Crashing is not**: `abort`, a trap, a failed contract and a slice index out of range, so a function that indexes a slice can be pure.
- **Across files**: each compile leaves a summary of its object (in a project under `.smith/effects/`, otherwise `main.o.fx` beside it) and the link of objects that have one (`smith-cc a.o b.o`, so `smith build` too) checks the calls that cross files, in C and in modules: an unannotated function of another file is inferred, and a function no file defines is `c`. A call to a function declared in your project with no body in the file adds nothing at compile time and is judged at the link; an error found only then names the chain through the files and stops the link.
- **What a body reaches**: the calls of the file and the headers it includes, followed to a function of libc or libSystem (a table of smith's gives each its effects) or to a call the file cannot see into: a call through a function pointer, inline assembly and an Objective-C message are `c`, and so is a function declared in a system header that the table does not know.
- **The promise is what a caller sees**, not the inferred set. Recursion is no effect (it is an error, below). On a declaration with no source (a prebuilt library's header) the same attribute is a promise smith trusts: `[[smith::effects(alloc, net)]] int curl_perform(void *handle);`.
- **The fix**: an effects error carries a fix-it (a quick fix in the editor, `(fix available)` in `smith lsp --check`) that replaces the function's effects or pure attribute with `[[smith::effects(...)]]` holding the whole set it needs (declared and inferred), never adding a second.
- **Names are as written**: the link's error names functions as the source writes them (`bad`, `fx::lib::noisy`, a module's private `shout`), never as the lowering named them (`main_bad`): each summary carries the written name.
- **In the editor**: `smith lsp` judges the calls between files too, with the summaries the build made (`.smith/effects/`): an open file shows the link's error and chain at the call. Saving a file writes its summary in the build's place and has the other open files parsed again (clangd's `forceRebuild`, no text is sent), and a first pass in the background makes the summaries that are missing or older than their source, one at a time, then parses the open files again. While that pass runs a function no summary defines is not judged yet. The editor may briefly be out of date; the link decides.
- **Where it is checked**: in the compile of a file, whenever a function carries an attribute (in a module, in a `.smith` file with no `module` line and in C), and in the editor.
- **No recursion in a module**: a call on a cycle of the module's functions (`fib` calling itself, `is_odd` and `is_even` calling each other) is a compile error naming the cycle, `fib -> recursion::fib (recursion.smith:7)`. Write a loop, with an explicit stack when you need one. A cycle whose every call is `[[clang::musttail]] return f(...);` is allowed: a jump is a loop. A call through a function pointer, or into code with no source, is not followed. C and a `.smith` file with no `module` line are left alone.
- **Worst-case stack**: linking objects compiled by smith (`smith build`, or `smith cc a.c b.c -o p`) writes `<program>.stack` beside the program: one tab-separated row a function, `name`, its stack and the deepest path (`f -> h -> g`). Frames are the backend's own (the numbers of `-fstack-usage`, which smith asks of every compile to an object and removes after reading). A cycle of musttail calls takes no more; any other recursion (C allows it) and a frame that grows at run time (`alloca`, a variable-length array) are `unbounded`. A call into code with no source (printf, a pointer call, a prebuilt library) cannot be measured: the row says `at least N + unknown (calls: printf, qsort)`, and a runtime decides what an unknown is (no number is added, and nothing in the build reads an environment variable). The header lines say the build the numbers are for (`opt=0 address undefined`): a runtime reads the file of the build it runs.
- **Asking about a function you are given**: a generic that takes a function (`constexpr auto f`) states what it allows with `import smith::effects;` and `static_assert(effects::within(f, effects::alloc), "gradient: f may only allocate");`. The masks (`effects::none`, `alloc`, `files`, `net`, `console`, `clock`, `process`, `env`, `entropy`, `globals`, `c`) are `u32`, so `effects::alloc | effects::files` is a set. That is the only way to ask, and only inside a `static_assert`: there is no `effects::of` and no `effects::pure` (`within(f, effects::none)` says pure), and anywhere else `within` is true and checks nothing. The effects exist only after the file is read, so smith judges the assertion then: a function of the file at once, one of another file at the link. The error shows your message, the effects and the chain (`viaNoisy -> noisy (a.smith:23) -> printf (a.smith:18)`), with a note at the call that instantiated the generic. A function with no source and no attribute is `c`, so `within` rejects it.
- **Promises**: what smith trusts and does not check is a function with an effects attribute and no source (a prebuilt library's header) and a function that calls `contract::trusted()`. `smith check` ends with `smith check: promises: N`, and `smith check -v` lists each as `file:line name`. They come from the summaries of the last build (`.smith/effects/`), so build first; a header's promise counts once, and smith's own generated files under `.smith/` are left out.
- **Not yet** (issue #207, #263): hover.

Overview: `smith-lang`. Reference: [knowledge/effects-check.md](../../../knowledge/effects-check.md).
