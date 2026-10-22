# `.smith` files

A `.smith` file is C with modules. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant. A file opts
into more, step by step: imports and sized type names work in any `.smith` file; a `module` line makes it a module, with
exports and private names, and a function with `constexpr type T` parameters is generic. The design and the issues it answers are in
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
  - **A foreign C symbol** is declared with `extern` and no `export`: `extern char **environ;`, `extern u32 counter;`,
    `extern u32 triple(u32 x);`. It keeps its C name (no prefix, no `__asm__` label, not in the header), so the module
    links to the symbol C defines and uses it bare, as C does. For a symbol a header declares, `import <h>;` and
    `c::name` do the same (`c::optind`, `c::stdout`); `extern` is for one no header declares (macOS's `<unistd.h>` has no
    `environ`). `export extern T name;` is the opposite: the module's own symbol, defined in C by the prefixed name.
  - `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and
    globals. An export has the symbol `game::replay::open` (an `__asm__` label), so no C name can collide with it.
    `main` stays `main`.
    An exported variable is declared before its definition in the module's own compile (`extern T name __asm__(…);`,
    what its header says), as a C file that includes its own header has it, so a `-Werror` project with
    `-Wmissing-variable-declarations` compiles a module with no pragma.
  - **`[[smith::c_name("Name")]]` before `export` chooses the C name** of the export, for a library that keeps its C
    API when it becomes a module (`[[smith::c_name("SmithArena")]] export typedef struct {…} Arena;`,
    `[[smith::c_name("smith_text_parse_f64")]] export bool parse_f64(…)`).
    - What C sees is exactly that name: the generated header declares `SmithArena`, and the symbol of a function or
      variable is that name too (no `__asm__` label: `_smith_text_parse_f64` on macOS), so C and C++ call it, define
      an export that has no body in the module, and link against it as against any C function. Other exports keep the
      prefixed name and the `::` symbol.
    - A `.smith` importer still writes the module's name (`arena::Arena`, `text::parse_f64`) and gets the C name: the
      header's `// smith-names:` line lists `Arena=SmithArena`, which is how the importer's lowering, its diagnostics
      and its editor know the pair. In the module itself the name is the C name in the generated code.
    - It goes on the export's first name (the tag, in `export typedef struct S {…} S;`, and the typedef of the same
      name with it); an enumeration's constants keep the prefixed names. It is a string holding one C identifier.
    - Errors: a C name another export already has chosen; one that the module's prefix gives another name (a private
      one included); a name declared later that the prefix would make the same; `[[smith::c_name]]` not followed by
      `export`; two on one export; an export that declares no typedef, tag, function or variable; a string that is no
      identifier (or a keyword), or an argument that is no string. It is the only attribute in its `[[ ]]`.
  - **A parameter keeps its name** when the module exports one by that name (`export void step(Step *step)`,
    `export i32 area(i32 width)` beside an export `width`): the parameter is not the export, so the header and the
    body say `step` and `width`, as C scopes them (inside the body the export of that name is out of reach, as in C). A
    local declared in a body is not tracked this way: it is renamed like the export, consistently.
  - A C name used with no `c::` is an error in every `.smith` file, a module or not: a function, a variable, a type,
    a tag or a macro that an `import`ed header declared. Names from `#include`d headers keep working bare, as in C.
    A file with no `module` line lowers nothing else: its names are not prefixed and nothing becomes `static`.
    Inside an attribute (`[[gnu::format(printf, 1, 2)]]`, `__attribute__((format(printf, 1, 2)))`) a word is the
    attribute's own, not a use of a C name: `printf` there needs no `c::`.
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

`--smith-lower` keeps the file's preprocessor directives as written, each on a line of its own at its place between the
tokens: `#include`, `#define`, `#undef`, `#pragma`, `#error`, `#warning`, `#line` and the conditionals (`#if`, `#ifdef`,
`#ifndef`, `#elif`, `#else`, `#endif`), so the compiler that builds the lowered C decides them. It evaluates none, so every
branch of a conditional is lowered, by the same plain path as the rest (a branch the build machine does not take needs no
answer from a parser: sized types are the compiler's built-in types and `c::NAME` is `NAME`; `tests/smith-lang/cases/lowered-directives`).
Left out: comments, and a chain that starts with `#if 0` (its text is not read; its `#else` stands alone and an `#elif B`
becomes `#if B`). A `#define` whose body has `::` or a sized type is an error at that line, since it would reach the
compiler as written ("make it a function"). The compiler's own run needs none of this (it has preprocessed them); a
`#pragma` such as `STDC FP_CONTRACT` reaches the hook as an annotation token and goes to the parser as it is.

## The bootstrap

smith's own build compiles some `.smith` sources (the library `smith::text` today; the others follow as they are ported),
and a fresh clone has only a C23 compiler: no hook, no installed smith that knows the newest language. So the build
compiles their lowering instead, committed in `bootstrap/` at the path of the source (`lib/smith/text.smith` is
`bootstrap/lib/smith/text.c`), with `config->cc`, a plain `cc` included. `build.c` lists the sources in
`build_bootstrap_sources`; a source joins the list when smith's build compiles it, and the file is then written by:

- `smith build bootstrap`, with the smith just built; and checked by `check-bootstrap` (part of `check`, like
  `check-headers`), which lowers again and fails naming each stale file. `tests/bootstrap.sh` (`smith build
  test-bootstrap`) tests the files.

A file is a first line (`// Generated by smith from lib/smith/text.smith. Do not edit; run smith build bootstrap.`), `// NOLINTBEGIN`
(and `// NOLINTEND` at the end: `smith lint` compiles it with the generator, and it is not formatted either, see
`.clang-format-ignore`), an include of the module's generated header (`../../../include/smith/random.h`, relative to the
file, which also finds the same file when smith writes it to `.smith/bootstrap/lib/smith/`), and `smith cc --smith-lower` of
the source less what the header already says (every `#include`, and a conditional chain that holds only includes; `typedef
struct {...} Name;` blocks and one-line `constexpr`, `extern` and `typedef` declarations), because one translation unit may
include both (two anonymous structs of one translation unit are two types, and C has no way to say they are one, so the
lowering cannot repeat the header's blocks; a fix in the lowering would have to know the header's text, as the script does): smith's own runtime compiles `io`,
which includes `<smith/random.h>`, together with the library. `scripts/bootstrap.sh` writes it. It needs no compiler, so the
stage0 writes the same bytes, and it asks nothing of the machine: the lowering's questions to the parser (`lookup`,
`type_info`, `field`, `read_header` in `smith_lang_run_raw`) are all unanswered, sized types become the compiler's
built-in `__UINT32_TYPE__` and its kin, and `c::NAME` becomes `NAME`. There is no SDK, include path or environment in it
(`tests/bootstrap.sh` lowers with `env -i` and `SDKROOT=/nonexistent`, and with the stage0). Exports with no `c_name`
keep their `__asm__("_smith::x::f")` labels, the only extension of C in the file; it compiles under Apple's `/usr/bin/clang`
and `cc` with `-std=gnu23 -Wall -Wextra -Wpedantic -Werror`. Its code is the code of the `.smith` compiled by smith
(same assembly at `-O2`), except for the line numbers `assert` prints, which are the lowered file's.

The lowering keeps the module's preprocessor directives as written (see "Lowering"), so the compiler that builds the C
decides them: a module may use `#pragma STDC FP_CONTRACT OFF`, `#if defined(__aarch64__)` chains, `#error`,
`#pragma clang diagnostic` and `#include` (`smith::random` does). Left out: comments (the
`.smith` source keeps them), and the use of `::` or a sized type in a `#define` body, which is an error (use an inline
function). The correct names (a module's name against a C header's) are checked by the real compile of the `.smith` that
smith's tests still do, not by the raw lowering.

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

Exports are separated by a blank line. A header that is committed (smith's own libraries, `include/smith/*.h`) is also
formatted with the repository's `.clang-format` by `smith build headers`, and `smith fmt` keeps it so; the `// Generated
by smith` and `// smith-` lines stay whole because that style's `CommentPragmas` says so (an importer reads them by line).

It has `extern "C"` guards and the module's imports as `#include`s, and it lists them in a `// smith-imports:` line,
which is how an import cycle is found, and the exports in a `// smith-names:` line (`// smith-names: Replay version open`),
which is how an importer knows the names it can write back (see "Names as written"); an export with a chosen C name is listed as `member=CName`. Both depend on the exports only, so
the header's bytes do not change with an edit to a body (an `export inline` function's body is in the header), or to the
lines above an export (a `#line` for each declaration would, and every importer would be built again on most edits). Importers include it (they need `-I .smith/include`); C and C++ callers include it
too; an export with no body is defined against it, in C, by its C name.

What the header keeps of the source (written by the plain lexer, `runtime/lang_trivia.c`; the entries are `SmithTrivia`):
- **Doc comments.** The comments directly above an `export` (or above the `[[smith::c_name]]` before it; no blank line
  between) are copied above its declaration, as written. Comments inside an exported declaration (a struct's fields) and a
  comment after its last token on the same line (`export u32 limit = 4; // bytes`) are kept. A comment separated by a
  blank line from the export, and the comments of private code, are not.
- **Conditional directives.** An `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` and `#endif` around exports are copied into
  the header verbatim, with the branches before an export in it, and nothing is preprocessed at header time: the includer's
  `-D` flags choose, as for any C header. A chain with no export in it is left out. `#if 0 … #else` writes the live text
  with no conditional. Because every branch of a chain is read (the lexer has no macros), a name an export declares in two
  branches is one export, and an importer's `// smith-names:` line lists it whichever branch is live.
- **Includes.** An `#include` (and an `import <h>;`) is written to the header, in source order, inside the conditionals it
  stands in: a module can choose between two spellings, `#if __has_include("arena.h") #include "arena.h" #else #include
  <smith/arena.h> #endif`, and the header says the same chain (`smith::random`: smith's own build, with no include path,
  finds the quoted one beside the header; the module's compile finds `<smith/arena.h>` by `-I .smith/include`). The `#if
  0` text is left out, and a conditional with no include in it is not written above the includes.
- **Inline functions.** `export inline T f(…) { … }` is written to the header as `static inline T game_replay_f(…) { … }`,
  body included, and the module defines it the same way: it has no symbol and no `__asm__` label, so C, C++ and `.smith`
  importers compile it themselves. Its body may use only what the header has: the module's other exports, the module's
  imports and C. A private name in it is an error in the includer, not in the module.
- An interface read in memory (`import foo` with no header) keeps the conditionals on the module's own lines, and no
  comments (diagnostics point at the module's lines).

## `smith build`

`smith_object` takes a `.smith` source like any other (docs/build-c.md, "`.smith` sources"). For each module it adds a
step that writes the module's header (`smith-cc --smith-header src/a/b.smith -o .smith/include/a/b.h`, restat: the file
is written only when its bytes change); every `.smith` compile gets `-I .smith/include` and waits for all the headers
and for one check, which reads each header's `// smith-module:` and `// smith-imports:` lines and fails on an import
cycle, naming it, before any compile starts. What an importer depends on is its depfile, which lists the headers it
read. So an edit to a module's body recompiles that module alone, and an edit to its exports recompiles its importers.
The rule that a module `a::b` lives in a file whose path ends in `a/b.smith` is checked once, when build.c runs.

## Tests and proofs

`smith_tests` finds tests in `.smith` files (`export void test_x(void) {` in a module, `void test_x(void) {` in a file with
no `module` line), and the runner and its reports name them as written: `geometry::box_test::area`, and a failure shows the
`.smith` line, its source and `geometry::box_test::test_area_fails` in the chain (docs/build-c.md, "Tests"). `smith prove`
compiles a `.smith` file as `smith cc` does and names its functions, calls and counterexample values as written
(`geometry::box::clamp`). Frames of a library written as the modules `smith::test`, `smith::contract` and `smith::trace`
are machinery in a report, as `smith_test_*` is. A module named `smith::test` itself writes its header to
`.smith/include/smith/test.h`, the path of the C library's header, so the library cannot become that module until the header
path is settled.

## Formatting

`smith fmt` formats a `.smith` file as C (clang-format 23 with `patches/clang-format-smith.patch`, about 100 lines): C's keywords
only (so a C name such as `throw` is a name, not C++'s keyword), `::` one token, and `module ...;` and `import ...;` lines at
file scope kept whole, `import <sys/wait.h>;` included. Inside a function body, `import` and `module` are names, so
`import value = c.x + c.y;` is an indented declaration of a type named `import`. Every C file formats the same as `.c` and as
`.smith` (`tests/smith-fmt.sh`). `smith fmt` with no arguments formats the project's `.smith` files
(every file git sees). clangd, which uses the same library, formats `.smith` files the same way; understanding the
rest of a `.smith` file (hover, diagnostics, completion) is `smith lsp` (below).

## Names as written

The compiler sees `geometry_box_Box`; the source says `box::Box` where it imports the module and `Box` in the module
itself. Everything that shows a name to a person shows the written one: the compiler's diagnostics (`error: use of
undeclared identifier 'box::aera'; did you mean 'box::area'?`, `note: 'box::area' declared here` at
`geometry/box.smith:5:12`) and, in `smith lsp`, hover, the outline, signature help, hints and completion. The way back is
a table the lowering keeps of the names it made (`runtime/lang_names.c`, `smith_lang_written_find`), never a guess at
underscores (`a_b` can be a real C name): the module's own declarations, the exports of each imported module (from the
`// smith-names:` line of its header) and of the modules those import (written with their whole module name, as the
file has no name for them), and every `x::name` the file wrote, declared or not. `smith lsp` takes the table from the
hook when a parse ends (`shim/names.h`) and a layer between the editor and clangd (`shim/lsp.cpp`) rewrites the answers
(`SMITH_LSP_RAW=1` turns the layer off: the compiler's names). In an editor, after `box::` the module's exports are
offered, each as its member, and the edit replaces only what comes after the `::` (`box::` + `area`). The layer also
leaves out of references and renames the generated headers (`.smith/include/*.h`), which are no place to go to or edit.

The debugger shows them as well (issue #27): lldb's frames, `bt` and types say `geometry::box::area` and
`geometry::box::Box`, with the whole module name (the same in every object, so the types of two objects are one type),
from `patches/clang-debug-name.patch` and the table above; see docs/debugging.md for what lldb then accepts (a cast to
`geometry::box::Box` does; `b box::area` does not, `b geometry::box::area` does).

## Linting

`smith lint x.smith` (clang-tidy 23 of smith's LLVM, in process) reads a `.smith` file as `smith cc` does and reports as
the source is written: the same hook, the same rule for the command, and the table of names above. `smith lint` with no
arguments lints what the project's `lint` task lists; what follows is what clang-tidy does with a `.smith` file it is
given (`tests/lint-smith.sh`).

- **The command.** clang-tidy builds the command of each file with clang's driver, not with `clang::createInvocation`, so
  it is `shim/tidy.cpp` that applies `smith_lang_command` (`-x c`, and `-std=gnu2y` unless a standard is named): the file
  is `clang/lib/Tooling/ArgumentsAdjusters.cpp` whole, with one call in `getStripPluginsAdjuster`, the last adjuster
  clang-tidy appends. smith defines every symbol of that object, so the linker leaves the archive's out. A command with
  no `.smith` input is returned as it was (clang-tidy runs each file with a command of its own, so a run of `.c` and
  `.smith` files is right for each), and a compile database's `-x c -std=gnu2y` is already the rule.
- **The findings.** A check reports from an engine of its own, not the compiler's, so clang's diagnostics (the lowering's
  among them) went through the hook's consumer and a check's did not. When a check is created `shim/tidy.cpp` notes that
  engine, and the hook of a `.smith` file puts a consumer in front of it (`smith::presentThrough`): the names in a
  message, a note and a fix-it are the written ones, as for the compiler's. Two things differ for a tool. The first
  declaration of an exported function is the lowering's copy of its prototype, and a check reports a name once, at the
  first declaration it finds, so the finding is moved to the token the copy was made from (the definition's name) and
  a fix-it in a copy is dropped (the definition's own is what changes the source). And a finding in the interface of an
  imported module (the generated header, or the module's source read as one) is dropped: the user did not write that text
  there, and the module's own file is linted as itself. The lowering's own error uses a built-in diagnostic in a tool, so
  that its number is not mistaken for one of a check's.
- **The names.** `readability-identifier-naming` judges the name the source wrote. A module's file-scope names are
  `geometry_box_Box` to the AST, which is no `CamelCase`, so `shim/tidy.cpp` registers, after clang-tidy's own modules, one
  that wraps every check's factory and replaces this one's with `SmithNaming`, which holds the real check (it is `final`,
  and its decisions private) and asks it, for a name the lowering made, about the member name (`Box`, `area`):
  the declaration is given that name for the one question and the old one back. The table is the lowering's own for the
  file (never a guess at underscores), so `a_b` that is a real C name is judged as it is. The message and the fix-it
  (which replaces the token the source has) are about the written name. Locals, parameters, fields and every other check
  see the AST as it is, and a `.c` file, whose names are never in the table, is untouched. An importer's use of a
  module's badly named export is reported once, in the module's file; a fix there does not rename the importers.
- **Not done.** Checks that look at a name (a function, a variable) in a message are shown the written one, but a check
  that compares names in the lowered form (a length or a reserved-name check, say) still sees
  `geometry_box_x`; `readability-identifier-naming` is the one that has been made to see the written name.

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
    text becomes the contents the file manager has for the source file, which the include then reads. Unlike the header
    on disk it keeps the module's lines: each declaration is on its line (the first line is `#pragma once`) and its name
    at its column where the text before allows, so a diagnostic that points into it is at the declaration in the module,
    `foo.smith:3:12`. The `#include` lines it needs are entered by the hook in front of it, its imports of other modules
    found the same way are rewritten to include their sources, and `// smith-module:`, `// smith-imports:` and
    `// smith-names:` are what the lowering of an importer reads of it (`SmithInterface`, `runtime/lang.h`).
  - **The parser's tokens, for tools.** Output that stands for a token of the source (a renamed name, the head of an
    exported declaration) is pushed back as a plain stream and passes the hook again without being lowered a second
    time (`replayed`, "Replay" in `shim/hook.cpp`); what the source has no token of (a `static`, the `__asm__` label) is
    reinjected. A reinjected stream is not reported to the Preprocessor's token watcher, which is how clangd's
    `TokenBuffer` (behind hover, references, highlights, the outline) learns the parser's tokens, so without this none
    of an exported declaration's head, its name included, was known to them and hover at a definition said nothing.
    The prototype that carries an exported function's symbol is a copy of the head: its tokens are placed in a buffer of
    their own (`relocate`), so that the declaration it makes is not a second one at the definition's place, and a
    diagnostic there is left out (the definition has its own).
  - **Diagnostics as written** go through a consumer in front of clang's (`Presenter`, `shim/hook.cpp`, in `smith cc` and
    in clangd alike): every message, note and fix-it is rewritten by the lowering's table of names, and a note into a
    module's generated header goes to the declaration in the module's source (`smith_lang_declarations` reads the
    module again for where it declares each export, so the header need not say). Uses of a module that could not be
    imported are kept out by the same consumer, by location.
- **The questions** the lowering asks the parser (`SmithHost` in `runtime/lang.h`) are answered from `Sema`'s name lookup
  in the current scope, so locals and parameters count and only completed declarations are visible (the hook runs one
  token ahead of the parser): is this name declared, and a type or a value; was it declared by an `import`ed header; and
  for a typedef or tag, its size, alignment and fields (names and the spelling of their types). The lowering uses the
  first two; the others are the questions first-class types and reflection will ask (issue #15), built and tested now
  (`SMITH_LANG_ASK=Packet,struct Header` prints what the parser answers at the end of the file). A type written as
  tokens (`const char *`, a typedef, `struct S`, the sized types' own names, pointers) is asked with `type_query`: its
  canonical spelling (typedefs looked through, the identity of a generic's instance), size, alignment, and whether it
  is an integer, a float, signed, a pointer, a bool, an enum or a record (`SMITH_LANG_ASK='?const char *,?Packet'`).
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
  `.smith` parses in a clangd run one at a time. And clangd does not know the extension, so every command it compiles a
  `.smith` file with gets the rule `smith cc` applies (`smith_lang_command`, `runtime/lang_command.c`, one function
  that both call): `-x c` before the file (before the `--` of clangd's commands), and `-std=gnu2y` unless the command names
  a standard. `smith lsp` applies it in `shim/clangd.cpp`, which defines `clang::createInvocation` (the one function that
  turns clangd's command into a compiler invocation: for the open file, the preamble and the background index) as the
  upstream function plus that call, so it holds for the fallback command of a folder with no `compile_commands.json`
  (`clang main.smith`), a hand-written database without `-x c`, and a command borrowed from a `.c` entry
  (`-x c-header`). The `compile_commands.json` that `smith build` writes carries `-x c -std=gnu2y` already, so a project
  is read exactly as it is built. With no database at all, a single file or a few modules in one folder just work:
  `import foo` is found by `foo.smith` beside the importing file, as for `smith cc main.smith foo.smith`, and the
  interface is made in memory (go-to-definition from `foo::f` lands in `foo.smith`). A real project wants its build.c's
  `compile_commands.json`, for its flags and include paths. The module headers are
  read through the file manager's file system, so a relative `-I .smith/include` means the command's directory. Because a
  module starts with code, clangd's preamble for it is empty, and the "translation unit with no declaration" extension
  warning that C gives for that is turned off for an empty main buffer. The messages go through `shim/lsp.cpp`:
  `smith_lsp_run` runs clangd's main with its input and output behind a pair of pipes, and a thread each way reads the
  protocol's messages, notes the requests (hover, completion, outline, signature help, hints, references, rename,
  workspace symbols) and rewrites the names in their answers with the table of the file asked about; a message that is
  none of those passes as the bytes it came in. The cursor of a completion that came right after `x::` is given to the
  lowering as a token (`SmithToken.completion`), which takes the `x::` away and leaves the cursor, so that the parser
  offers names there and the layer keeps the module's. A `c::name` that is no macro (`c::printf`, `c::FILE`) stays a
  plain name in the main file, at its place, in the parser's tokens that clangd's token buffer records (a name pushed
  back as a reinjection is not recorded, and a hover, a definition or a reference at it found no node, so
  go-to-definition fell back to the index by name and named every declaration of it, the SDK's and libc++'s `using`);
  a macro (`c::EOF`) is expanded from a stream of its own and clangd finds it as a macro. `tests/lsp.sh` drives a real
  session.
- **Routing**: `smith cc x.smith` runs clang as `-std=gnu2y -x c x.smith`; the extension turns the hook on. A command
  that says `-x c` itself keeps it and gets the standard (`-x c++` or another language is left alone). A smith whose clang
  has no hook (a stage0, or an LLVM built before the patch) refuses a `.smith` input with a message that says so.

## Generic functions

A function of a module whose parameter list has `constexpr type T` parameters is written once and compiled for each
type it is called with (issue #15). `constexpr` in a parameter list means compile-time: the parameter is in no
generated function. `type` is a kind, as `u32` is, so `constexpr type T` is a compile-time constant that is a type. A
bare `type T` is an error that says to write `constexpr type T`.

```c
module arena;

export T *push(Arena *a, constexpr type T) {        // the types may be used anywhere in the signature and body
    usize aligned = (a->used + alignof(T) - 1) & ~(alignof(T) - 1);
    ...
}

Point *p = arena::push(&a, Point);                  // the type is the argument in T's place
```

- **What a generic is.** A function with a body at file scope in a module, exported or not, `inline` or not (not
  `static` or `extern`: `export` is what makes it public). Up to 4 `constexpr type` parameters and 16 parameters. It has
  no symbol and cannot be named without being called (`&arena::push` is an error); C and C++ see a placeholder
  (`[[clang::unavailable]] void arena_push(void);`), and the module's header carries the record of the generic in a
  `// smith-generic:` comment line, which an importer reads (so a header on the include path, as `smith build` writes,
  or the module's source read in memory, both work).
- **A call** writes the type as the argument in the place of the parameter (`arena::push(&a, Point)`,
  `list::make_two(arena, i32)`), or leaves every type argument out and has them inferred (below). A call is read whole,
  so it may be an argument of another call, 16 deep at most. The call becomes
  `__extension__ ({ extern R f(P) __asm__("label"); f(args); })` with the instance's name for `f`.
- **An instance** is one function for each distinct list of types, compared by the parser's canonical spelling (`u32`
  and `uint32_t` are one instance). It is written at the end of the file as a weak, hidden definition with a symbol of
  its own, `_pick::larger(int)`, so the linker folds the copies other files made; an `inline` generic is
  `always_inline` instead. Its C name is `pick_larger__i32`. A generic can call generics (`list::make(a, T)` in a
  body: `T` is the instance's type), 32 deep and 4096 instances at most.
- **Inference.** A call that leaves out all the type arguments has each `T` told from the arguments of the parameters
  that say it: `T x`, `const T *x`, `T *x`, `T x[]` (`*` and `[]` each dereference the argument's type once). The
  compiler types a name, a field chain (`p.x`, `p->y`), `*e`, `&e`, `e[i]`, a call, a cast and a literal, in the scope
  the parser is in (a local declared by an earlier statement counts). A literal adapts to the arguments that are not
  one (`larger(x, 1)` is the type of `x`). Two arguments that disagree are the error "conflicting types for `T`: i32 and f64";
  an argument it cannot type (arithmetic, a conditional) is an error that says to write the type. Inside a generic's
  body the locals of the instance are not yet known to the parser, so write the type there.
- **`if constexpr`** in a generic's body is chosen for each instance; the branch not taken is never given to the parser,
  so it may name what `T` has not (`value.x`). The condition is a constant expression of integers (`true`, `false`,
  the operators of C), `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (canonical spellings), and the questions
  `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum` and `is_record`. A type is written as C
  writes one: `T`, a sized type, a typedef, `struct S`, qualifiers, `*`. Anywhere else a generic's constraint is a
  `static_assert` in a final `else`. `if constexpr` outside a generic is not understood by the parser.
- **Errors** point at the generic's source, with a note where the instance was asked for, chained when one generic
  asked for another: `num.smith:5:14: error: invalid operands to binary expression ('Point' and 'Point')`, then
  `main.smith:13:20: note: in instance num::add(Point), requested from here`. An error in a generic's head, a call that
  cannot be written (wrong number of arguments, a type that cannot be inferred, a statement that ends inside the
  parentheses, the end of the file, too deep) is one error: the generic or the call is dropped for a name whose error is
  not shown, so nothing follows it.
- **The editor and the debugger.** Hover on the name in a call shows the generic as its module wrote it and the
  instance (`generic function pick::larger`, the head, `instance pick::larger(i32)`), and go-to-definition goes to the
  generic's name. An instance is `pick::larger<i32>` in `smith dbg` and `smith dap`, and its code is the generic's own
  file, line and column, so a breakpoint, a listing and the arguments are those of the source.
- **Not done:** generic types and functions that return `type`, compile-time value parameters (`constexpr u32 tile`),
  overloading, specialization, variadic type lists, constraints other than `if constexpr` and `static_assert`,
  `if constexpr` outside a generic, pointers to a generic, and a type local to a function as `T`.

## Known limits

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros: a macro in an exported signature is not seen by it. An export under `#if`
  works (the directives are copied), but `#define`s are not, and the header pass does not evaluate a condition.
- A `c::NAME` that is a macro cannot be passed as a macro argument (the argument is expanded before the lowering sees it).
- Variables declared with a struct in one declaration (`export struct S {…} s;`) are not given a symbol.
- A hover in an importer shows the declaration as the header gives it (`int32_t foo::f(int32_t x)`, from the header's
  standard C types, where the module says `i32`); the `asm("_foo::f")` label of the symbol is left out (`shim/lsp.cpp`).
- A rename of an exported name edits what clangd knows of (the module, and the importers its index has seen); it does
  not edit the generated headers. Like any cross-file rename in clangd it misses an importer the index has not read yet.
- A fix-it that a mistyped `x::name` offers puts in the member alone (`f`), so it makes `foo::f`.
- A note into an interface read in memory (no database) shows, in `smith cc`, that interface's line under the caret.
- `-x smith` is not a clang language: the `.smith` extension and `-x c` stand for it.
- `clang -E` and `-Xclang -dump-tokens` print the source text under a token the lowering made or renamed (it keeps the
  location of the token it replaces, so diagnostics, the debugger and clang-tidy's fixes point at the `.smith` text);
  the parser, the code and the debug info are unaffected. `SMITH_LANG_DUMP` shows the macro arguments of a call after
  the call, and `smith cc --smith-lower` is the lowering of the text as written.
- On macOS a backtrace names an export `_game::replay::open`, since the label is used as written (the underscore is the
  Mach-O prefix of a C symbol); `b game::replay::open` finds it.
