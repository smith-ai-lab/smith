---
type: contract
title: A module's generated C header
description: A module's header includes exactly what its exports need, and C and C++ code calls a module through it.
tags: [headers, c-interop]
---
# A module's generated C header

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

It has `extern "C"` guards and the includes its exports need (below), and it lists the modules it imports in a `// smith-imports:` line,
which is how an import cycle is found (every import is listed, whether or not the header includes it), and the exports in a `// smith-names:` line (`// smith-names: Replay version open`),
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
- **Includes: exactly what the exported declarations need, and nothing else.** An export's prototype, an exported typedef,
  the fields of an exported struct or union, an exported variable's type, an enum, the body of an exported `inline`
  function and the record of an exported generic are the *exported declarations*; the header includes what they use:
  - `<stddef.h>` and `<stdint.h>`, always (the sized types, `size_t`).
  - A module imported with `import a::b;` when an exported declaration writes `b::name`; one only a body uses is not
    included (it is still in `// smith-imports:`). It is included **relative to the header**, as a quoted include looks
    in the including file's folder first: `smith::random` importing `smith::arena` writes `#include "arena.h"`, and
    `game::main` importing `lib::text` writes `#include "../lib/text.h"`. So the headers of one tree (`include/smith`,
    `.smith/include/smith`, an installed copy) find each other with no include path of their own, which smith's own
    build needs: `smith.c` includes `include/smith/random.h` with no `-I`, and so does a stage0 compiling it alone
    (`tests/headers_test.smith` compiles each header with and without an include path). The in-memory interface an importer
    reads from a module's source is included from the include path, as the importer wrote it.
  - A C header imported with `import <h>;` or `import "h";` when an exported declaration writes a name of it as
    `c::name` (`struct c::tag` is a tag); one only a body uses is not included. With one import (or the spellings of one
    header in the branches of a conditional, below) that is all there is to decide. With several, the header pass
    asks the compiler which import declared each name: it runs `smith cc -fsyntax-only` on a file of nothing but the
    module's imports (so, in a smith with clang inside, the token hook's `SMITH_LANG_ASK_IMPORTS`, answered by the
    parser's lookup in `shim/hook.cpp`), and the import is the one the module wrote (`<stdio.h>`), never an internal
    header the name was found in (`<_stdio.h>`). Where it is, a name is attributed to the first import that brought its
    declaration in. The compile flags `smith build` gives the module's compile that decide which header is found
    (`-I`, `-D`, `-std=`, ...) are given to the header step, and `smith cc --smith-header f.smith -o f.h [flags]` takes them. A stage0, or a probe that does not
    compile, cannot answer: the header then includes every import of the module when an export uses a `c::` name
    (more than needed, never less).
  - An `#include` never reaches the header, because the names of an `#include`d header are written bare and the header
    pass has no parser to say which header declares one. A module whose exports need a header's names `import`s it and
    writes them `c::name` (`import <smith/arena.h>;` and `c::SmithArena` for a C library's type). A name that only an
    `#include` declares, used in an export, is not defined in the header: the includer's compile fails, and
    `tests/headers_test.smith` (every committed generated header compiles on its own, as C and as C++) finds it.
  A conditional around imports (`#if defined(__linux__) import <linux/x.h>; #else import <sys/x.h>; #endif`) is
  copied with the imports it holds: when one import of a conditional is kept, so are the others of it (they are other
  spellings of it), and a conditional with none kept is left out. The `#if 0` text is left out.
- **Inline functions.** `export inline T f(…) { … }` is written to the header as `static inline T game_replay_f(…) { … }`,
  body included, and the module defines it the same way: it has no symbol and no `__asm__` label, so C, C++ and `.smith`
  importers compile it themselves. Its body may use only what the header has: the module's other exports, the module's
  imports and C. A private name in it is an error in the includer, not in the module.
- **The instance section: what an exported generic's instance needs of the module's private code** (issue #75). An
  instance is compiled in the importer's file, so it needs a private function's or variable's symbol and a private
  type's or constant's definition there, and an importer must still not write any of them. The header pass keeps every
  private declaration as the lowering writes it (a function or a variable as the declaration that carries its symbol,
  `int32_t m_helper(int32_t x) __asm__("_m::helper");`, with no body or initializer; a type or a `constexpr` in full),
  marks the ones the bodies of the exported generics name, and the ones those name in turn (a prototype's parameter
  type, the size of an array), and writes only the marked, in the order declared, between `#ifdef SMITH_INSTANCES` and
  `#endif`, after the exports (and a `// smith-linked: helper counter` line, the functions and variables among them, and
  a `// smith-hidden:` line, all the names of the section). C, C++ and `.smith` code that is not an importer never see
  the section: `shim/hook.cpp` defines `SMITH_INSTANCES` around the `#include` of the header it enters for an
  `import`. What the section names through imports (`c::FILE`) is included by the header, like the record of an
  exported generic; an unmarked private declaration adds nothing, so the header is as wide as its exports and
  generics need. The module's own compile reads `// smith-linked:` from its header (the interface made from its source
  when there is none) and gives those functions and variables the symbol `_m::helper` instead of `static` (and drops
  `inline`); every other private name stays `static`. An importer's `m::helper` is an error (``is private to its module
  ... export it``), since it is in `// smith-hidden:`. A private generic that an exported one calls has its record in
  the header too, marked private in its `// smith-generic:` line (`p`, `q` if inline); an importer cannot call it, and
  nothing else of it is written. An exported declaration may still not name a private type, and a private constant its
  array bound or initializer needs must be exported.
- An interface read in memory (`import foo` with no header) keeps the conditionals on the module's own lines, and no
  comments (diagnostics point at the module's lines).
