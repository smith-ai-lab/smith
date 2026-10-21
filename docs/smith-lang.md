# `.smith` files

A `.smith` file is C with modules. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant. A file opts
into more, step by step: imports and sized type names work in any `.smith` file; a `module` line makes it a module, with
exports and private names. The design and the issues it answers are in
[issue #11](https://github.com/adam-r-kowalski/smith/issues/11); this page is what is built.

```c
// src/game/replay.smith
module game::replay;                // must match the path: .../game/replay.smith
import lib::text as tx;             // a smith module, bound to `tx`
import <string.h>;                  // a C header; its names are reached as c::name

export typedef struct { u64 frames; tx::Text name; } Replay;
export constexpr u32 version = 3;
export u32 counter = 7;
export Replay open(const char *path) { … c::strlen(path) … }
export u64 checksum(const u8 *data, usize size);   // no body: defined in C or C++
u32 helper(void) { … }                              // private
```

## The rules

- **Imports** work in any `.smith` file and come before any declaration (`#include` stays allowed anywhere).
  - `import a::b::c;` binds `c`; `import a::b::c as x;` binds `x`. Members are reached with `::`: `x::open`,
    `x::Replay`, `struct x::Pair`. `x::` is always a module and `x.` always a value, so a variable may share its
    module's name (`io::Io io = io::make();`): `io` is a binding only where `::` follows it, and never inside an
    attribute (`[[gnu::unused]]`). This needs C23, which lexes `::` as one token.
  - `import <h>;` and `import "h";` include a header and bind `c`: `c::sinf(x)`, `c::EOF`, `c::WIFEXITED(s)`,
    `struct c::stat`. A quoted header is looked for beside the importing file first.
  - `c::` names C only in a file that imports a header; `c` is otherwise any identifier, and `c.x` is a field.
- **Modules.** The first line, `module a::b;`, makes the file a module; its path must end in `a/b.smith`.
  - Every file-scope name in a module is prefixed with the module's name (`game_replay_open`). `export` makes a
    declaration public; everything else is `static` (functions and variables) and used nowhere else.
  - `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and
    globals. An export has the symbol `game::replay::open` (an `__asm__` label), so no C name can collide with it.
    `main` stays `main`.
  - A C name used in a module with no `c::` is an error: a function, a variable, a type, a tag or a macro that an
    `import`ed header declared. Names from `#include`d headers keep working bare, as in C.
- **Sized types**, in any `.smith` file: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize` (`size_t`), `isize` (`ptrdiff_t`),
  `f16` (`_Float16`), `f32`, `f64`. They are spellings of the standard types, never typedefs. A file that declares its
  own `u32` (or a variable named `u8`) keeps it.
- **Errors**, each pointing at the `.smith` text: an exported declaration naming a private type; an import cycle
  (`a -> b -> a`); `export` in a file with no `module` line; a module name that
  does not match the path; a bare C name in a module; an import after a declaration; a module that is not found; the
  `module` line not first; an export that declares two names.
- **Not yet:** selective imports, re-exports, a module over several files.

## Lowering

A `.smith` file is lowered to C as the compiler reads it; there is no intermediate file. `smith --smith-lower x.smith`
prints the lowering (without macros or the parser's help, so `c::EOF` shows as `EOF`); `SMITH_LANG_DUMP=<file>` writes
what the compiler's own run lowered. The corpus (`tests/smith-lang/cases`) keeps both for every rule.

## The header

Each module has a generated C header, `.smith/include/game/replay.h`, written by `smith --smith-header
src/game/replay.smith -o …` from the tokens alone: the module is not compiled, so every header can be written first and
then every compile can run in parallel.

```c
typedef struct { uint64_t frames; lib_text_Text name; } game_replay_Replay;
constexpr uint32_t game_replay_version = 3;
extern uint32_t game_replay_counter __asm__("_game::replay::counter");
game_replay_Replay game_replay_open(const char *path) __asm__("_game::replay::open");
```

It has `extern "C"` guards and the module's imports as `#include`s, and it lists them in a `// smith-imports:` line,
which is how an import cycle is found. Importers include it (they need `-I .smith/include`); C and C++ callers include it
too; an export with no body is defined against it, in C, by its C name.

## How it works

- **The hook** is one patch to smith's clang (`patches/clang-token-hook.patch`, about 100 lines, applied by
  `scripts/llvm.sh`): `clang::TokenHook`, offered tokens as they leave `Preprocessor::Lex`, after macro expansion and
  before the parser. It may consume tokens and put others back with `EnterTokenStream`, or change the token where it is,
  and is told when the translation unit's `Sema` exists. Tokens replayed from the lookahead cache are not offered twice.
  A tool links a factory in; nothing else of clang changed. smith's factory (`shim/hook.cpp`) answers only for a main
  file called `*.smith`.
  - **It is offered little.** An identifier carries a bit (`IdentifierInfo::setHookWatched`); the hook is offered the
    watched identifiers and the end of the file, and, when it asks, the braces (a module's function bodies) or every
    token (a module's declarations). The Preprocessor keeps the last eight tokens for it (`getHookPrevious`), so a
    watched identifier still knows what came before it. Text of other files than the main one is never offered. Plain C
    in a `.smith` file is therefore not slower to compile than in a `.c` file, to within what can be measured
    (under 1%).
  - **What is watched:** `module`, `import`, `export`, the sized type names, each import binding (`c` included), and in a
    module its own file-scope names and the names its imported headers declared.
- **The questions** the lowering asks the parser (`SmithHost` in `runtime/lang.h`) are answered from `Sema`'s name lookup
  in the current scope, so locals and parameters count and only completed declarations are visible (the hook runs one
  token ahead of the parser): is this name declared, and a type or a value; was it declared by an `import`ed header; and
  for a typedef or tag, its size, alignment and fields (names and the spelling of their types). The lowering uses the
  first two; the others are the questions first-class types and reflection will ask (issue #15), built and tested now
  (`SMITH_LANG_ASK=Packet,struct Header` prints what the parser answers at the end of the file).
- **The lowering** is C (`runtime/lang*.c`, included by smith.c): a state machine over tokens that knows nothing of
  clang. A module's file-scope declaration is read whole before it is written (its prefix, `static`, and for an exported
  function a prototype with the symbol label are decided by all of it). The same code runs over a plain lexer
  (`runtime/lang_lex.c`) for `--smith-header`.
- **Routing**: `smith cc x.smith` runs clang as `-x c x.smith`; the extension turns the hook on. A smith whose clang
  has no hook (a stage0, or an LLVM built before the patch) refuses a `.smith` input with a message that says so.

## Known limits

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros or `#if`: a macro in an exported signature, or an export under `#if`, is
  not seen by it.
- A `c::NAME` that is a macro cannot be passed as a macro argument (the argument is expanded before the lowering sees it).
- Variables declared with a struct in one declaration (`export struct S {…} s;`) are not given a symbol.
- Diagnostics show lowered names (`game_replay_open`); mapping them back to `game::replay::open` is the consumer's work.
- `-x smith` is not a clang language: the `.smith` extension and `-x c` stand for it.
- `clang -E` and `-Xclang -dump-tokens` print the source text under a token the lowering made or renamed (it keeps the
  location of the token it replaces, so diagnostics, the debugger and clang-tidy's fixes point at the `.smith` text);
  the parser, the code and the debug info are unaffected. `SMITH_LANG_DUMP` shows the macro arguments of a call after
  the call, and `smith --smith-lower` is the lowering of the text as written.
- On macOS a backtrace names an export `_game::replay::open`, since the label is used as written (the underscore is the
  Mach-O prefix of a C symbol); `b game::replay::open` finds it.
