---
name: smith-lang-modules
description: "Modules in .smith files: `module a::b;`, `import a::b::c as x;`, `export`, `x::name`, private names, cycles. Load when writing or reading .smith source with module, import or export, on `::` between names, or on errors: 'import cycle', 'is private; export it', 'must be in a file whose path ends in', 'imports come before any declaration', 'cannot find module', 'needs a `module` line'."
---

# Modules

This skill ships in the smith plugin, from `.agents/skills/smith-lang-modules/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`.

```c
// src/game/replay.smith
module game::replay;                 // the path must end in game/replay.smith
import lib::text as tx;              // binds `tx`; `import lib::text;` binds `text`
export typedef struct { u64 frames; tx::Text name; } Replay;
export constexpr u32 version = 3;
export Replay open(const char *path) { … tx::make(path) … }
export u64 checksum(const u8 *data, usize size);   // no body: defined in C or C++
u32 helper(void) { … }                              // private
```

## The rules

- `x::name` reaches a module's member (`struct x::Pair`, `x::version`); `x.` is always a value. A variable may share its module's name (`io::Io io = io::make();`): `io` is a binding only where `::` follows, and never inside an attribute (`[[gnu::unused]]`).
- `import` comes before any declaration; `#include` stays allowed anywhere.
- `export` applies to functions, typedefs, struct/union/enum tags, enum constants, `constexpr` constants and globals, one name per declaration. Everything else in a module is private (`static`).
- An export's symbol is `game::replay::open` (underscore first on macOS). `main` stays `main`.
- Names in the module become `game_replay_open` in C; diagnostics show that spelling.

## Not yet

Selective imports, re-exports, a module over several files, `smith interface`.

## Errors (each at the `.smith` text)

- an exported declaration names a private type: export that type;
- `import cycle: a -> b -> a`; a module imports itself;
- `module a::b` in a file whose path does not end in `a/b.smith`; the `module` line must be first;
- `export` in a file with no `module` line; `export static`; two names in one export;
- `imports come before any declaration`; `cannot find module`: its header is not in `.smith/include`: pass the module's source to `smith_object` in build.c and `smith build` writes the header (by hand: `smith cc --smith-header`, with `-I .smith/include`).
- `smith: import cycle: a::x -> a::y -> a::x` (from `smith build`, before any compile); `smith_object: src/x.smith declares module a::b, so its path must end in a/b.smith` (build.c, once).

For C names inside a module, load `smith-lang-c-interop`.
