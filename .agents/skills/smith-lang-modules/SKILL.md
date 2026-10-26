---
name: smith-lang-modules
description: "Modules in .smith files: `module a::b;`, `import a::b::c as x;`, `x::name`, private names, cycles, one command for a few modules. Load when writing or reading .smith source with module or import, on `::` between names, or on errors: 'import cycle', 'is private; export it', 'must be in a file whose path ends in', 'imports come before any declaration', 'cannot find module', 'needs a `module` line'."
---

# Modules

```smith
// src/game/replay.smith
module game::replay;                 // the path must end in game/replay.smith
import smith::text as tx;            // binds `tx`; `import smith::text;` binds `text`
export struct Replay { // the tag is the type's name: `Replay`, no typedef
    u64 frames;
    const char name[:];
};
export constexpr u32 version = 3;
u32 helper() { // private
    return 1;
}
export Replay open(const char *path) {
    return (Replay){.frames = version + helper(), .name = tx::make(path)};
}
```

C has a header, a `static` and a naming prefix for this; a module has one file, and what it does not `export` is private. What `export` takes, its C names and a body defined in C: `smith-lang-exports`. C headers and `c::`: `smith-lang-c-interop`. What goes in a module: `smith-idiom`.

## Rules

- **Every `.smith` file starts with its `module` line**, from its path (`smith lint` says which, check `smith-module`): `main.smith` is `module main;`, `primes_test.smith` is `module primes_test;`, `tests/build_test.smith` too (`tests/` is a root of its own), `build.smith` is `module build;`. A file with no module line compiles as plain C with sized types and `import`, so a renamed `.c` works; adding the line is the first step of a port.
- `x::name` reaches a module's member (`struct x::Pair`, `x::version`); `x.` is always a value. A variable may share its module's name (`io::Io io = io::make();`): `io` is a binding only where `::` follows, and never inside an attribute (`[[gnu::unused]]`).
- An import named like the module's own last name takes `x::`: in `module build;` with `import smith::build;`, `build::app(...)` is the import's and the module's own `build` is unqualified (`export void build(build::Build *b)` keeps the C name `build`).
- **A struct's, union's or enum's tag is a type name** (a payload enum's too): `struct Node { Node *next; };`, `struct Pending;` then `Pending *p;`, `enum Color { red, green };` then `Color c;`, also for a tag inside a function. `struct Point` still works. A tag and a variable, function or enum constant of one name are an error; `c::` tags are the header's, and a file with no `module` line keeps C's rule (a `typedef struct` there is C's own). `typedef struct {...} Point;` still compiles.
- `import` comes before any declaration; `#include` stays allowed anywhere.
- A parameter is not the export that shares its name: `export void step(Step *step)` keeps `step` in the header and the body, as C scopes it.
- **A few modules in one command**: `smith cc main.smith foo.smith -o prog`. `import foo` with no `foo.h` on the include path reads `foo.smith`: among the command's `.smith` files first, then at `dir/foo.smith` for a module whose root is `dir` (`a::b` is `dir/a/b.smith`; the root of `module game::main;` in `src/game/main.smith` is `src`; a file with no `module` line has its own folder). The interface is made in memory, nothing is written, link every source as in C. `smith build` writes the headers instead, and they win.
- Not yet: selective imports, re-exports, a module over several files, `smith interface`.

## Errors

`import cycle`, `cannot find module`, `is private; export it`, a path that does not match the `module` line, `imports come before any declaration`, a stale header: `smith-lang-modules-errors` lists each with its fix. For C names (`import <h>;` puts them under `c::`), `smith-lang-c-interop`.
