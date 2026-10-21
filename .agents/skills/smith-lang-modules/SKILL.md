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
- **One command builds a few modules**: `smith cc main.smith foo.smith -o prog`. `import foo` with no `foo.h` on the include path reads `foo.smith`: among the `.smith` files of the command first, then at `dir/foo.smith` for a module whose root is `dir` (`a::b` is `dir/a/b.smith`; the root of `module game::main;` in `src/game/main.smith` is `src`, and a file with no `module` line has its own folder). `smith cc -c main.smith` alone finds `foo.smith` by that path. The interface is made in memory and nothing is written; link every source, as in C. `smith build` writes the headers instead and they win.
- `export` applies to functions, typedefs, struct/union/enum tags, enum constants, `constexpr` constants and globals, one name per declaration. Everything else in a module is private (`static`).
- An export's symbol is `game::replay::open` (underscore first on macOS). `main` stays `main`.
- Names in the module become `game_replay_open` in C; diagnostics show that spelling.
- **`[[smith::c_name("SmithArena")]] export typedef struct {…} Arena;`** chooses the export's C name, for a library whose C API must not change: the header declares `SmithArena`, and a function or variable's symbol is that name (no `::` label), so C defines and calls it plainly. `.smith` importers still write `arena::Arena` and get the C name. It goes before `export`, alone in its `[[ ]]`, on the first name the export declares (an enumeration's constants keep the prefixed names). Errors: a C name another name has (chosen, or made by the prefix, private names included), not followed by `export`, two on one export, an export declaring no typedef, tag, function or variable, a string that is no C identifier.
- **`export inline`** defines the function in the module's header too, as `static inline` with its body (no symbol), so C, C++ and `.smith` importers compile it themselves; its body uses only exports, imports and C. Comments above an export and `#if` lines around exports reach the header (load `smith-lang-c-interop`).
- **`extern` without `export`** declares a foreign C symbol: it keeps its C name (no prefix, no label, not in the header), for `environ` or a variable or function a C file defines. `export extern` is the module's own symbol, defined in C by its prefixed name. Load `smith-lang-c-interop`.

## Not yet

Selective imports, re-exports, a module over several files, `smith interface`.

## Errors (each at the `.smith` text)

- an exported declaration names a private type: export that type;
- `import cycle: a -> b -> a`; a module imports itself;
- `module a::b` in a file whose path does not end in `a/b.smith`; the `module` line must be first;
- `export` in a file with no `module` line; `export static`; two names in one export;
- `imports come before any declaration`; `cannot find module`: neither its header nor its source was found. The message lists what was tried (the header on the include path, the module among the command's `.smith` files, and the path the name implies). Name the module's source on the command (`smith cc main.smith foo.smith`), put it at that path, or in a build pass its source to `smith_object` in build.c and `smith build` writes the header (by hand: `smith cc --smith-header`, with `-I .smith/include`). It is the one error: the uses of the module's names report nothing more. A module whose own source has an error cannot be imported (``module `foo` cannot be imported: foo.smith:3:12: ...``): fix that file first.
- `smith: import cycle: a::x -> a::y -> a::x` (from `smith build`, before any compile); `smith_object: src/x.smith declares module a::b, so its path must end in a/b.smith` (build.c, once).

For C names (an `import <h>;` puts them under `c::` in every `.smith` file, a module or not), load `smith-lang-c-interop`.
