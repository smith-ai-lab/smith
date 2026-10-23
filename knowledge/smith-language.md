---
type: architecture
title: The .smith language
description: A .smith file is C with modules, lowered to C as clang reads it by a token hook, with sized types, the rules, one command, and what the language does not do yet.
tags: [smith-lang, lowering]
---
# The .smith language

**`.smith` files are C with modules, lowered to C as clang reads them** ([the language](smith-language.md), issue
#11). The one change to clang is a token hook on its `Preprocessor` (`patches/clang-token-hook.patch`, about 100
lines, applied by `scripts/llvm.sh`), which offers a `TokenHook` the identifiers it watches, after macro expansion and
before the parser; plain C in a `.smith` file costs the compiler under 1%. The language is smith's own C
(`runtime/lang*.c`, which knows nothing of clang), and the clang side is `shim/hook.cpp`: the hook, and the parser's
name lookup as the lowering's questions. A second patch (`patches/clang-debug-name.patch`, about 60 lines, a process-wide
hook in `CGDebugInfo`) lets the debugger show a module's names as written: `geometry::box::area` for a function or variable
(its `DW_AT_name`), `Box` in namespaces `geometry::box` for a type; plain C debug info is byte-identical
(see [the debugger](debugger.md)). Modules use `::` (`module a::b;`, `x::name`, `c::printf`), never dots. The
`.smith` extension turns it on (`smith cc x.smith` is `-x c -std=gnu2y x.smith`: `.smith` is smith's own language and
compiles as C2y, the standard `smith new` uses, so `auto`, `::` and `_Countof` work, unless the caller names a
`-std=`, which wins); plain C in a `.smith` file means what it means in a `.c` file (`tests/smith-lang-c.sh`).
`smith cc main.smith foo.smith` just works: an `import foo` that finds no `foo.h` on the include path reads the
module's source (among the command's `.smith` files, else at the path its name implies under the importing module's
root) and makes its interface in memory, writing nothing; `smith build` still writes the headers and they win. The
sized types (`u32`, `f64`, ...) are typedefs, made when a file first uses one it does not declare itself, so a
diagnostic reads `'i32' (aka 'int')`; a generated C header still says the standard C types. **A module's generated header includes exactly what its exported declarations need**: `<stddef.h>`, `<stdint.h>`, an imported module an export writes `x::Name` of, and a C header (`import <h>;`) an export writes `c::name` of (with several imports smith asks the compiler which declares it; a stage0 keeps them all); an `#include`, and an import only a body uses, never reach it, so a module whose exports use a header's types `import`s it (`tests/headers.sh`). The lowering reports a C
keyword used as a name (`i32 double(i32 x)`) itself, at the name. A smith whose clang lacks the hook, a stage0 included, refuses a `.smith`
input; it can still write a module's header (`smith cc --smith-header`). The compiler's helpers are under `smith cc`
(`--smith-header`, `--smith-lower`, `--smith-which`), since smith's top level is only verbs. `smith build` builds the
modules itself: a header step per module, and every `.smith` compile, and every C compile that names a header of `.smith/include` as an input or puts that folder on its include path, waits for all of them (knowledge/build-file.md).

A `.smith` file is C with modules. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant.
**The C rule:** valid C in a `.smith` file without a `module` line means exactly what it means in C, at the same `-std` as a `.c` file: the latest C by default; an older C is an explicit `-std=`. A new feature uses
syntax that is invalid C (`::`, `import x;`, `auto` and `constexpr type` parameters, `if constexpr`); a `module` line may
reject valid C (it defines exports and private names) but never silently changes what a construct means. A module-only
rejection gets an error case in the corpus (`tests/smith-lang/cases/error-*`), and `tests/smith-lang-c.sh` pins the rule:
smith's own C sources, and the programs of `tests/smith-lang-c/` (array parameters, `sizeof` of one, an `int a[*]`
prototype, the language's words as names, ordinary constructs), give the same code and output as `.c` and as `.smith`. Known exceptions, both issues: an enum
constant or function named like a sized type fails loudly (#91), and the default dialect is C2y, not the `.c` default (#92).
A file opts
into more, step by step: imports and sized type names work in any `.smith` file; a `module` line makes it a module, with
exports and private names, and a function with `constexpr type T` or `auto` parameters is generic. The design and the issues it answers are in
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
      name with it); an exported enumeration's constants are `<CType>_<Value>` in C, the C name of the enum (this one's, or its tag's, `mix_modes_Level_High`) and the constant, which is C's own naming rule (`MixMode_Fast`), while `.smith` importers write `modes::Fast`; an anonymous enum typedef without it keeps the prefixed names. It is a string holding one C identifier.
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
    attribute's own, not a use of a C name: `printf` there needs no `c::`. The keywords of C23 that a header may define as
    a macro (`static_assert`, `alignas`, `alignof`, `bool`, `true`, `false`, `nullptr`, `thread_local`) are the
    language's, not C names.
- **Sized types**, in any `.smith` file: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize` (`size_t`), `isize` (`ptrdiff_t`),
  `f16` (`_Float16`), `f32`, `f64`. They are typedefs of the standard types (`typedef __INT32_TYPE__ i32;`), so the same
  types (`%llu`, `_Generic` and a C header's `uint64_t` agree) and a diagnostic names them as written:
  `initializing 'i32' (aka 'int')`. The typedef is made when the file first uses the name and declares nothing of that
  name, so a file that declares its own `u32` (a variable, a file-scope function or an enum constant named `u8`) keeps it, and a C header's `typedef uint32_t
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
  - **A keyword where a name goes**: after a sized type name or a typedef of the module (and `*` or a qualifier), a C keyword that cannot continue
    the type is the name of something. ``foo.smith:3:12: error: `double` is a C keyword; it cannot name a function``,
    and likewise a variable, parameter, field or type. The name is then read as an identifier, so clang adds no cascade
    (a later use of the misnamed thing in an expression, `return int;`, is still clang's "expected expression"). A
    keyword after a typedef of the module (`Box double`) is reported the same way, as one error; after any other type
    (`Point double(...)` with a typedef from a header) it is clang's own message.
- **Not yet:** selective imports, re-exports, a module over several files.

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

`i32 main(io::Io *io)` in a `.smith` file (it needs `import smith::io;`, under any name) is the program's body, with the
io made for it: the lowering writes it as `static int32_t smith_user_main(SmithIo *io) { ... }` (the module's prefix is
never put on it) and, at the end of the file, `int main(int argc, char **argv)` that makes `SmithIo io =
smith_io_make(argc, argv)`, calls `smith_user_main(&io)`, runs `smith_io_close(&io)` and returns the status. It is found
by looking ahead from the `i32` that begins a file-scope declaration (`i32`, `main`, `(`, the binding, `::`, `Io`, `*`,
a name, `)`), in a file with a `module` line or none and in `--smith-lower`; the queued `main` is renamed, so the rest
of the lowering sees a private function. The C `main` has the location of the name you wrote (the debugger shows
it at your `main`'s line), the table of names says `smith_user_main` is written `main`, the debug info names it `main` (the debug-name hook of `shim/hook.cpp`, with a module line or none, `tests/dap.sh`), and `io::make` anywhere in the
file is the error "main already has the io". The C forms (`i32 main(i32 argc, char **argv)` in a module, `char *argv[]` too in a file with no `module` line, and `int main(void)`) lower
as they always did, and `.c` files are never rewritten. Tests: the cases `main-io*` and `error-main-io-make`.

**Array parameters keep their size in a module** (issue #81). Valid C in a file with no `module` line means what it means in
C, so only a file with a `module` line is changed, and each change is a rejection. A parameter `T xs[N]` is N objects:
the lowering writes it `T xs[static N]` in the code (`smith_lang_array_size`, `runtime/lang_head.c` for a held head,
`smith_lang_head_step` for a private one; the generated header keeps `T xs[N]`), which clang turns into a non-null,
`dereferenceable(N * sizeof(T))` pointer that `smith prove` reads (#74's `[static N]` mechanism, no second one). A parameter
with no size is an error at its declaration. `countof(xs)` is rewritten, in the module's bodies (`smith_lang_countof_parameter`,
`runtime/lang_sparse.c`), to the parameter's own size tokens (kept by `smith_lang_array_token` as the head streams by,
three dimensions of four tokens, eight parameters per function); the host watches `countof` once a function has one.
The other rejections are about the AST, so they cannot be tokens: `ArrayParameters` (`shim/hook.cpp`) walks the functions
the module defines when the translation unit is complete (the hook is Sema's external source and does its work when
Sema asks for the tentative definitions, at the end of the unit: no destructor of Sema's is called with `-disable-free`),
reading `ParmVarDecl::getOriginalType`, which clang keeps under the pointer: a constant index out of range, `++`, `--` and
assignment of the parameter, `sizeof(xs)`, and an argument to a module's function (declared in the file or in a
generated header; a C function takes the pointer) that is a pointer or an array of another shape. Tests: the cases
`array-param*` and `error-array-param-*`, and section 11a of `tests/prove.sh`. In an editor (`smith lsp`) the AST checks
depend on clangd reaching the end of the unit.

`smith_object` takes a `.smith` source like any other (knowledge/build-file.md). For each module it adds a
step that writes the module's header (`smith-cc --smith-header src/a/b.smith -o .smith/include/a/b.h`, restat: the file
is written only when its bytes change); every `.smith` compile gets `-I .smith/include` and waits for all the headers
and for one check, which reads each header's `// smith-module:` and `// smith-imports:` lines and fails on an import
cycle, naming it, before any compile starts. What an importer depends on is its depfile, which lists the headers it
read. So an edit to a module's body recompiles that module alone, and an edit to its exports recompiles its importers.
The rule that a module `a::b` lives in a file whose path ends in `a/b.smith` is checked once, when build.c runs.

`smith_tests` finds tests in `.smith` files (`export void test_x(void) {` in a module, `void test_x(void) {` in a file with
no `module` line), and the runner and its reports name them as written: `geometry::box_test::area`, and a failure shows the
`.smith` line, its source and `geometry::box_test::test_area_fails` in the chain (knowledge/build-tests.md). `smith prove`
compiles a `.smith` file as `smith cc` does and names its functions, calls and counterexample values as written
(`geometry::box::clamp`). Frames of a library written as the modules `smith::test`, `smith::contract` and `smith::trace`
are machinery in a report, as `smith_test_*` is. A module named `smith::test` itself writes its header to
`.smith/include/smith/test.h`, the path of the C library's header, so the library cannot become that module until the header
path is settled.

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros: a macro in an exported signature is not seen by it. An export under `#if`
  works (the directives are copied), but `#define`s are not, and the header pass does not evaluate a condition.
- A `c::NAME` that is a macro and an argument of another macro (`c::assert(n < c::SIZE_MAX)`) is expanded with the
  argument, before the lowering sees it: the lowering drops the `c::` before an expansion and the macro is allowed
  because the text before it says `c::` (`tests/smith-lang/cases/c-macro-argument`). One that expands to nothing
  there is not understood. `static_assert`, `alignas`, `alignof`, `bool`, `true`, `false`, `nullptr` and `thread_local`
  are keywords of C23 and need no `c::` even where a header defines them as macros (`<assert.h>` does `static_assert`).
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
