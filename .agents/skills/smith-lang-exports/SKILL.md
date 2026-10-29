---
name: smith-lang-exports
description: "What a module's `export` takes and how an export is named in C: exported functions, typedefs, tags, constants, globals; the symbol game::replay::open and the C name game_replay_open; [[smith::c_name(\"X\")]], export inline, extern with and without export, a body defined in C. Load when writing `export`, [[smith::c_name]], `export inline` or `extern` in a module, or on a C name clash or an `export static` error."
---

# What `export` takes and how C sees it

```smith
module game::replay;

[[smith::c_name("SmithReplay")]] export typedef struct { u64 frames; } Replay;   // C sees SmithReplay
export constexpr u32 version = 3;                 // constants and globals export too
export u32 after(u32 n) { return n + version; }
export [[gnu::always_inline, maybe_unused]] inline u64 doubled(u64 n) { return n * 2; }   // body in the header: no symbol
export u64 checksum(const u8 *data, usize size);  // no body: C or C++ defines game_replay_checksum
extern char **environ;                            // a foreign C symbol: private, keeps its name
```

In C this is a header, a prefix and a convention; here the symbol is `game::replay::open` (underscore first on macOS) and C writes `game_replay_open`; diagnostics show the C spelling. `main` stays `main`. Modules: `smith-lang-modules`; the generated header and calling from C: `smith-lang-c-headers`.

## Rules

- `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and globals, one name per declaration. Everything else is private (`static`).
- **`[[smith::c_name("SmithArena")]] export typedef struct {...} Arena;`** chooses the export's C name, for a library whose C API must not change: the header declares `SmithArena`, a function or variable's symbol is that name (no `::` label), and `.smith` importers still write `arena::Arena`. It goes before `export`, alone in its `[[ ]]`, on the first name the export declares. An exported enum's constants are `<CType>_<Value>` in C (`[[smith::c_name("MixMode")]] export typedef enum { Fast, Slow } Mode;` gives `MixMode_Fast`; a tagged `export enum Level` gives `mix_modes_Level_High`); Smith importers write `modes::Fast`.
- **`export inline`** defines the function in the header as `static inline` with its body, so C, C++ and `.smith` importers each compile it. Its body uses only exports, imports and C, never a private name. An `if constexpr` on `target::` there becomes `#if` in the header.
- **`extern` without `export`** declares a foreign C symbol (`environ`, or a variable or function a C file defines): it keeps its C name (no prefix, no label), is private and not in the header. **`export extern T name;`** is the module's own symbol, defined in C by its prefixed name or its `c_name`.
- An export with no body is defined in C or C++ against the header, by its C name.
- The comments above an export and `#if` lines around exports reach the header (`smith-lang-c-headers`).

## Errors

- a C name that another name has (chosen, or made by the prefix, private names included); `c_name` not followed by `export`, two on one export, on an export declaring no typedef, tag, function or variable; a string that is no C identifier.
- `export static`; two names in one export; `export` in a file with no `module` line.
- an exported declaration naming a private type, or an array bound naming a private constant: export it.
