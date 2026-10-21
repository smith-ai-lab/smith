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
    `struct c::stat`. A quoted header is looked for beside the importing file first. An import always puts the
    header's names under `c::`, in a module or not; `#include` keeps them bare, as in C.
  - `c::` names C only in a file that imports a header; `c` is otherwise any identifier, and `c.x` is a field.
- **Modules.** The first line, `module a::b;`, makes the file a module; its path must end in `a/b.smith`.
  - Every file-scope name in a module is prefixed with the module's name (`game_replay_open`). `export` makes a
    declaration public; everything else is `static` (functions and variables) and used nowhere else.
  - `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and
    globals. An export has the symbol `game::replay::open` (an `__asm__` label), so no C name can collide with it.
    `main` stays `main`.
  - A C name used with no `c::` is an error in every `.smith` file, a module or not: a function, a variable, a type,
    a tag or a macro that an `import`ed header declared. Names from `#include`d headers keep working bare, as in C.
    A file with no `module` line lowers nothing else: its names are not prefixed and nothing becomes `static`.
- **Sized types**, in any `.smith` file: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize` (`size_t`), `isize` (`ptrdiff_t`),
  `f16` (`_Float16`), `f32`, `f64`. They are typedefs of the standard types (`typedef __INT32_TYPE__ i32;`), so the same
  types (`%llu`, `_Generic` and a C header's `uint64_t` agree) and a diagnostic names them as written:
  `initializing 'i32' (aka 'int')`. The typedef is made when the file first uses the name and declares nothing of that
  name, so a file that declares its own `u32` (or a variable named `u8`) keeps it, and a C header's `typedef uint32_t
  u32;` is fine (it conflicts only where the types differ, and then clang says so). The typedefs are implicit: they are
  in no header. A module's generated C header says `uint32_t`, so a C file that includes it has no `i32`.
- **C is C2y** in a `.smith` file: `smith cc x.smith` adds `-std=gnu2y` (the standard `smith new` uses), so `auto x = f();`,
  `::` and `_Countof` work with no flag. A `-std=` of the caller's wins, and none is added beside a C++ input.
- **Errors**, each pointing at the `.smith` text: an exported declaration naming a private type; an import cycle
  (`a -> b -> a`); `export` in a file with no `module` line; a module name that
  does not match the path; a bare C name from an imported header; an import after a declaration; a module that is not found; the
  `module` line not first; an export that declares two names; a C keyword used as a name.
  - **A module that is not found** is one error, naming what was tried (``cannot find module `foo`: no `foo.h` in the
    include paths, no `foo.smith` among the input files, and nothing at the path the name implies (`src/foo.smith`)``),
    and the uses of its names (`foo::f`) report nothing more. The same for a cycle and for a module whose source has an
    error.
  - **A keyword where a name goes**: after a sized type name (and `*` or a qualifier), a C keyword that cannot continue
    the type is the name of something. ``foo.smith:3:12: error: `double` is a C keyword; it cannot name a function``,
    and likewise a variable, parameter, field or type. The name is then read as an identifier, so clang adds no cascade
    (a later use of the misnamed thing in an expression, `return int;`, is still clang's "expected expression"). A
    keyword after another type (`Point double(...)`) is clang's own message.
- **Not yet:** selective imports, re-exports, a module over several files.

## One command

```
smith cc main.smith foo.smith -o prog      # main.smith has `import foo;`; foo.smith is `module foo;`
smith cc -c main.smith                     # finds foo.smith by path, too
```

When `import foo;` finds no `foo.h` on the include path, smith looks for the module's source: first among the `.smith`
files on the command line (`SMITH_LANG_INPUTS`, which `smith cc` sets for the compiles it runs), then at the path the name
implies under the importing module's root (for `module main;` in `dir/main.smith`, `import foo` is `dir/foo.smith`, and
`a::b` is `dir/a/b.smith`; a file with no `module` line has its own folder for a root). The module's interface (what
`smith cc --smith-header` would write) is made in memory and read as the header, which the compile's depfile names by the
source; nothing is written. A header on the include path (as `smith build` writes them) is used instead, and is no
slower. Linking is as in C: name every source on the command line, or use a build.c. On a laptop under load, the compile
of a small `main.smith` took 21.5 ms with a header on the path and 22.0 ms reading the source (60 alternating runs).

## Lowering

A `.smith` file is lowered to C as the compiler reads it; there is no intermediate file. `smith cc --smith-lower x.smith`
prints the lowering (without macros or the parser's help, so `c::EOF` shows as `EOF`); `SMITH_LANG_DUMP=<file>` writes
what the compiler's own run lowered. The corpus (`tests/smith-lang/cases`) keeps both for every rule.

## The header

Each module has a generated C header, `.smith/include/game/replay.h`, written by `smith cc --smith-header
src/game/replay.smith -o …` (only when its bytes change) from the tokens alone: the module is not compiled, so every header can be written first and
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

## `smith build`

`smith_object` takes a `.smith` source like any other (docs/build-c.md, "`.smith` sources"). For each module it adds a
step that writes the module's header (`smith-cc --smith-header src/a/b.smith -o .smith/include/a/b.h`, restat: the file
is written only when its bytes change); every `.smith` compile gets `-I .smith/include` and waits for all the headers
and for one check, which reads each header's `// smith-module:` and `// smith-imports:` lines and fails on an import
cycle, naming it, before any compile starts. What an importer depends on is its depfile, which lists the headers it
read. So an edit to a module's body recompiles that module alone, and an edit to its exports recompiles its importers.
The rule that a module `a::b` lives in a file whose path ends in `a/b.smith` is checked once, when build.c runs.

## Formatting

`smith fmt` formats a `.smith` file as C (clang-format 23 with `patches/clang-format-smith.patch`, about 100 lines): C's keywords
only (so a C name such as `throw` is a name, not C++'s keyword), `::` one token, and `module ...;` and `import ...;` lines at
file scope kept whole, `import <sys/wait.h>;` included. Inside a function body, `import` and `module` are names, so
`import value = c.x + c.y;` is an indented declaration of a type named `import`. Every C file formats the same as `.c` and as
`.smith` (`tests/smith-fmt.sh`). `smith fmt` with no arguments formats the project's `.smith` files when its build.c lists them
(`smith new` globs `**/*.smith`). clangd, which uses the same library, formats `.smith` files the same way; understanding the
rest of a `.smith` file (hover, diagnostics, completion) is issue #26.

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
  - **The sized types are made by the hook**, with no macro in the way: at the first use of a sized name that nothing
    declares, the hook adds an implicit `TypedefDecl` to the translation unit (`Sema::PushOnScopeChains`), and the token
    stays the name as written, so clang's diagnostics say `i32` (aka `int`) and print no "expanded from macro" notes. To the
    lowering the name is still undeclared (the hook answers its questions that way), so a generated header gets the
    standard C types. Without a `Sema` (`clang -E`) the old macro text is used.
  - **After a sized type name** the hook looks at the next token whatever mode it is in (it is offered it, and passes over
    what it would not have been offered): a C keyword there that is no continuation of a type is reported by the lowering's
    own words (`smith_lang_keyword_name_use` reads the text around it for what it names) and read as an identifier.
    `smith cc --smith-header` does the same on its own tokens, so a bad module fails at its header.
  - **A module found by its source** is made in memory by `smith_lang_interface` (the header pass over a text; the
    lowering's tables are kept aside and put back, since an importer's own lowering is in the middle of its work). The
    text becomes the contents the file manager has for the source file, which the include then reads; its imports of other
    modules found the same way are rewritten to include their sources. Uses of a module that could not be imported are
    kept out of the diagnostics by a consumer in front of clang's (`Suppressing` in `shim/hook.cpp`), by location.
- **The questions** the lowering asks the parser (`SmithHost` in `runtime/lang.h`) are answered from `Sema`'s name lookup
  in the current scope, so locals and parameters count and only completed declarations are visible (the hook runs one
  token ahead of the parser): is this name declared, and a type or a value; was it declared by an `import`ed header; and
  for a typedef or tag, its size, alignment and fields (names and the spelling of their types). The lowering uses the
  first two; the others are the questions first-class types and reflection will ask (issue #15), built and tested now
  (`SMITH_LANG_ASK=Packet,struct Header` prints what the parser answers at the end of the file).
- **The lowering** is C (`runtime/lang*.c`, included by smith.c): a state machine over tokens that knows nothing of
  clang. An exported declaration is read whole before it is written (its prefix and, for a function, a prototype with
  the symbol label are decided by all of it); the plain lexer (`runtime/lang_lex.c`, for `smith cc --smith-header`) reads every
  declaration that way. A private declaration of a module is decided as it streams by, which is what keeps the hook
  cheap: `static` goes before the first token (from what that token is, and `main` from a look at the file's text), the
  name after a type is the one declared, and an aggregate's body, an initializer and the declarators after an aggregate
  need no look ahead. `smith_lang_fast` (`runtime/lang_declare.c`) answers each token as keep, rename, sized type or
  drop, or says it needs the slow path (`smith_lang_feed`), which can look ahead; the answer is given before anything
  was changed. `SMITH_HOOK_STATS=1` prints how many tokens each way took.
- **In clangd**: `smith lsp` installs the same hook (`smith_clangd_main` calls `smith_hook_install`), so every parse of
  a `.smith` file in clangd (the open file, its preamble, the background index, a completion) is lowered as a compile is,
  and the source locations the hook keeps are the positions the editor shows. Two things differ from a compile.
  The lowering is C with static tables and no malloc, so one exists per process; a clangd parses many files at once, so a
  hook takes it when it is made and gives it back at the end of its file (`Lowering` in `shim/hook.cpp`), and
  `.smith` parses in a clangd run one at a time. And `compile_commands.json` carries `-x c -std=gnu2y` for a `.smith` source
  (clangd does not know the extension; `smith cc` adds it itself), which `smith build` writes. The module headers are
  read through the file manager's file system, so a relative `-I .smith/include` means the command's directory. Because a
  module starts with code, clangd's preamble for it is empty, and the "translation unit with no declaration" extension
  warning that C gives for that is turned off for an empty main buffer. `tests/lsp.sh` drives a real session.
- **Routing**: `smith cc x.smith` runs clang as `-std=gnu2y -x c x.smith`; the extension turns the hook on. A smith whose clang
  has no hook (a stage0, or an LLVM built before the patch) refuses a `.smith` input with a message that says so.

## Known limits

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros or `#if`: a macro in an exported signature, or an export under `#if`, is
  not seen by it.
- A `c::NAME` that is a macro cannot be passed as a macro argument (the argument is expanded before the lowering sees it).
- Variables declared with a struct in one declaration (`export struct S {…} s;`) are not given a symbol.
- Diagnostics show lowered names (`game_replay_open`); mapping them back to `game::replay::open` is the consumer's work.
  So do hover, symbols and completion in `smith lsp`, whose completion after `x::` is empty and whose go-to-definition
  lands on the generated header until the background index has the module's source.
- `-x smith` is not a clang language: the `.smith` extension and `-x c` stand for it.
- `clang -E` and `-Xclang -dump-tokens` print the source text under a token the lowering made or renamed (it keeps the
  location of the token it replaces, so diagnostics, the debugger and clang-tidy's fixes point at the `.smith` text);
  the parser, the code and the debug info are unaffected. `SMITH_LANG_DUMP` shows the macro arguments of a call after
  the call, and `smith cc --smith-lower` is the lowering of the text as written.
- On macOS a backtrace names an export `_game::replay::open`, since the label is used as written (the underscore is the
  Mach-O prefix of a C symbol); `b game::replay::open` finds it.
