# `.smith` files

A `.smith` file is C with modules. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant. A file opts
into more, step by step: imports and sized type names work in any `.smith` file; a `module` line makes it a module, with
exports and private names. The design and the issues it answers are in
[issue #11](https://github.com/adam-r-kowalski/smith/issues/11); this page is what is built.

```c
// src/game/replay.smith
module game.replay;                 // must match the path: .../game/replay.smith
import lib.text as tx;              // a smith module, bound to `tx`
import <string.h>;                  // a C header; its names are reached as c.name

export typedef struct { u64 frames; tx.Text name; } Replay;
export constexpr u32 version = 3;
export u32 counter = 7;
export Replay open(const char *path) { … c.strlen(path) … }
export u64 checksum(const u8 *data, usize size);   // no body: defined in C or C++
u32 helper(void) { … }                              // private
```

## The rules

- **Imports** work in any `.smith` file and come before any declaration (`#include` stays allowed anywhere).
  - `import a.b.c;` binds `c`; `import a.b.c as x;` binds `x`. Members are reached with dots: `x.open`, `x.Replay`,
    `struct x.Pair`.
  - `import <h>;` and `import "h";` include a header and bind `c`: `c.sinf(x)`, `c.EOF`, `c.WIFEXITED(s)`, `struct
    c.stat`. A quoted header is looked for beside the importing file first.
  - `c` is a name only in a file that imports a header. In a file that does not, `c` is any other identifier.
- **Modules.** The first line, `module a.b;`, makes the file a module; its path must end in `a/b.smith`.
  - Every file-scope name in a module is prefixed with the module's name (`game_replay_open`). `export` makes a
    declaration public; everything else is `static` (functions and variables) and used nowhere else.
  - `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and
    globals. An export has the symbol `game.replay.open` (an `__asm__` label), so no C name can collide with it.
    `main` stays `main`.
  - A C name used in a module with no `c.` is an error: a function, a variable, a type, a tag or a macro that an
    `import`ed header declared. Names from `#include`d headers keep working bare, as in C.
- **Sized types**, in any `.smith` file: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize` (`size_t`), `isize` (`ptrdiff_t`),
  `f16` (`_Float16`), `f32`, `f64`. They are spellings of the standard types, never typedefs. A file that declares its
  own `u32` (or a variable named `u8`) keeps it.
- **Errors**, each pointing at the `.smith` text: an exported declaration naming a private type; an import cycle
  (`a -> b -> a`); a declaration reusing an import's name; `export` in a file with no `module` line; a module name that
  does not match the path; a bare C name in a module; an import after a declaration; a module that is not found; the
  `module` line not first; an export that declares two names.
- **Not yet:** selective imports, re-exports, a module over several files.

## Lowering

A `.smith` file is lowered to C as the compiler reads it; there is no intermediate file. `smith --smith-lower x.smith`
prints the lowering (without macros or the parser's help, so `c.EOF` shows as `EOF`); `SMITH_LANG_DUMP=<file>` writes
what the compiler's own run lowered. The corpus (`tests/smith-lang/cases`) keeps both for every rule.

## The header

Each module has a generated C header, `.smith/include/game/replay.h`, written by `smith --smith-header
src/game/replay.smith -o …` from the tokens alone: the module is not compiled, so every header can be written first and
then every compile can run in parallel.

```c
typedef struct { uint64_t frames; lib_text_Text name; } game_replay_Replay;
constexpr uint32_t game_replay_version = 3;
extern uint32_t game_replay_counter __asm__("_game.replay.counter");
game_replay_Replay game_replay_open(const char *path) __asm__("_game.replay.open");
```

It has `extern "C"` guards and the module's imports as `#include`s, and it lists them in a `// smith-imports:` line,
which is how an import cycle is found. Importers include it (they need `-I .smith/include`); C and C++ callers include it
too; an export with no body is defined against it, in C, by its C name.

## How it works

- **The hook** is one patch to smith's clang (`patches/clang-token-hook.patch`, applied by `scripts/llvm.sh`):
  `clang::TokenHook`, offered each token as it leaves `Preprocessor::Lex`, after macro expansion and before the parser.
  It may consume tokens and put others back with `EnterTokenStream`, and is told when the translation unit's `Sema`
  exists. Tokens replayed from the lookahead cache are not offered twice. A tool links a factory in; nothing else of
  clang changed. smith's factory (`shim/hook.cpp`) answers only for a main file called `*.smith`.
- **The questions** the lowering asks the parser (`SmithHost.lookup` in `runtime/lang.h`) are answered from `Sema`'s name
  lookup in the current scope: whether a name is declared, a type or a value, whether it is from an `import`ed header.
  The hook runs one token ahead of the parser, so only completed declarations are visible. `struct` fields and sizes are
  the same channel and not asked yet (issue #15).
- **The lowering** is C (`runtime/lang*.c`, included by smith.c): a state machine over tokens that knows nothing of
  clang. A module's file-scope declaration is read whole before it is written (its prefix, `static`, and for an exported
  function a prototype with the symbol label are decided by all of it). The same code runs over a plain lexer
  (`runtime/lang_lex.c`) for `--smith-header`.
- **Routing**: `smith x.smith` is compiled as `-x c x.smith`; the extension turns the hook on.

## Known limits

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros or `#if`: a macro in an exported signature, or an export under `#if`, is
  not seen by it.
- A `c.NAME` that is a macro cannot be passed as a macro argument (the argument is expanded before the lowering sees it).
- Variables declared with a struct in one declaration (`export struct S {…} s;`) are not given a symbol.
- Diagnostics show lowered names (`game_replay_open`); mapping them back to `game.replay.open` is the consumer's work.
- `-x smith` is not a clang language: the `.smith` extension and `-x c` stand for it.
