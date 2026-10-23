---
name: smith-lang-modules
description: "Modules in .smith files: `module a::b;`, `import a::b::c as x;`, `export`, `x::name`, private names, cycles. Load when writing or reading .smith source with module, import or export, on `::` between names, or on errors: 'import cycle', 'is private; export it', 'must be in a file whose path ends in', 'imports come before any declaration', 'cannot find module', 'needs a `module` line'."
---

# Modules

This skill ships in the smith plugin, from `.agents/skills/smith-lang-modules/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`. What to write in a module, and how to port C into one: `smith-idiom`.

```smith
// src/game/replay.smith
module game::replay;                 // the path must end in game/replay.smith
import smith::text as tx;            // binds `tx`; `import smith::text;` binds `text`
export typedef struct {
    u64 frames;
    tx::Text name;
} Replay;
export constexpr u32 version = 3;
u32 helper(void) { // private
    return 1;
}
export Replay open(const char *path) {
    return (Replay){.frames = version + helper(), .name = tx::make(path)};
}
export u64 checksum(const u8 *data, usize size); // no body: defined in C or C++
```

## The rules

- `x::name` reaches a module's member (`struct x::Pair`, `x::version`); `x.` is always a value. A variable may share its module's name (`io::Io io = io::make();`): `io` is a binding only where `::` follows, and never inside an attribute (`[[gnu::unused]]`).
- **An import named like the module's own last name takes `x::`**: in `module build;` with `import smith::build;`, `build::app(...)` is the import's and the module's own `build` is written unqualified (`build(b)`). That is the build file: `export void build(build::Build *b)` in `module build;` keeps the C name `build`.
- `import` comes before any declaration; `#include` stays allowed anywhere.
- **One command builds a few modules**: `smith cc main.smith foo.smith -o prog`. `import foo` with no `foo.h` on the include path reads `foo.smith`: among the `.smith` files of the command first, then at `dir/foo.smith` for a module whose root is `dir` (`a::b` is `dir/a/b.smith`; the root of `module game::main;` in `src/game/main.smith` is `src`, and a file with no `module` line has its own folder). `smith cc -c main.smith` alone finds `foo.smith` by that path. The interface is made in memory and nothing is written; link every source, as in C. `smith build` writes the headers instead and they win.
- `export` applies to functions, typedefs, struct/union/enum tags, enum constants, `constexpr` constants and globals, one name per declaration. Everything else in a module is private (`static`).
- A parameter is not the export that shares its name: `export void step(Step *step)` keeps `step` in the header and the body, as C scopes it.
- An export's symbol is `game::replay::open` (underscore first on macOS). `main` stays `main`.
- Names in the module become `game_replay_open` in C; diagnostics show that spelling.
- **`[[smith::c_name("SmithArena")]] export typedef struct {…} Arena;`** chooses the export's C name, for a library whose C API must not change: the header declares `SmithArena`, and a function or variable's symbol is that name (no `::` label), so C defines and calls it plainly. `.smith` importers still write `arena::Arena` and get the C name. It goes before `export`, alone in its `[[ ]]`, on the first name the export declares (an exported enumeration's constants are `<CType>_<Value>` in C: `[[smith::c_name("MixMode")]] export typedef enum { Fast, Slow } Mode;` gives `MixMode_Fast`, a tagged `export enum Level` gives `mix_modes_Level_High`, and Smith importers write `modes::Fast`; an anonymous enum without a C name keeps the prefixed names). Errors: a C name another name has (chosen, or made by the prefix, private names included), not followed by `export`, two on one export, an export declaring no typedef, tag, function or variable, a string that is no C identifier.
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
- ``import vector_check; found `vector_check.h`, which is a C header``: a C header with the module's name is on the include path (beside the file, with `-I .`) and was found instead of the header smith writes (the one with a `// smith-module:` line). Rename the C header or the module, or import the C header as `import "vector_check.h";`.
- ``use of undeclared identifier 'foo::bar'`` followed by the note ``the header of module `foo` lists no export `bar`; if the module has one, the header is stale``: clang's error (with its "did you mean") and then why: the module's header does not list `bar` (a typo, a private name, or a header from before the module had it, such as a project's old `.smith/include/smith/test.h`). `smith build` writes the headers again; `smith cc x.smith y.smith` needs none.
- `smith: import cycle: a::x -> a::y -> a::x` (from `smith build`, before any compile); `smith_object: src/x.smith declares module a::b, so its path must end in a/b.smith` (build.c, once).

For C names (an `import <h>;` puts them under `c::` in every `.smith` file, a module or not), load `smith-lang-c-interop`.
