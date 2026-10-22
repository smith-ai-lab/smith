---
name: smith-lang-c-interop
description: "C and C++ with .smith: `import <math.h>;`, `c::printf`, `c::EOF`, `c::WIFEXITED(s)`, generated C headers (.smith/include, game_replay_open), calling a smith module from C or C++, an export with no body defined in C. Load on 'is a C name; write c::', 'is a C macro', 'names a C tag', `#include` in a module, `__asm__` labels, or extern \"C\" against a module."
---

# C and C++ beside `.smith`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-c-interop/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`. `import <h>;` with `c::` names, never `#include`, is a rule of `smith-idiom`; a library that is a module (`import smith::arena;`) is used as a module, not through its C header.

## C headers into smith

```smith
import <math.h>;
import <stdio.h>; // printf and the like are raw effects: take a capability instead (smith-io)

f32 length(f32 x, f32 y) {
    return c::sqrtf(x * x + y * y);
}

bool at_end(i32 ch) {
    return ch < 0; // EOF is a C macro: c::EOF compiles, and smith lint (smith-preprocessor) reports it
}
```

`import <h>;` (or `import "h";`, looked for beside the file first) includes the header and binds `c`: `c::sqrtf(x)`, `c::EOF`, `c::WIFEXITED(s)`, `struct c::stat`. A macro name works in a compile (`c::EOF`, `c::WIFEXITED(s)`), but a macro is the preprocessor, and `smith lint` reports it (the check `smith-preprocessor`): the module that wraps the C API provides what the macro stood for as a function or a constant (`contract::assert`, `math::max(usize)`; `io` will write `EOF` and `errno` as what they are on each platform), and until there is one a line keeps the macro with `// NOLINT(smith-preprocessor)`. `c::` names C only in a file that imports a header; otherwise `c` is any identifier.

A name an imported header declared, used without `c::`, is an error in every `.smith` file, a module or not (a function, variable, type, tag or macro): `` `sqrtf` is a C name; write `c::sqrtf` ``, `` `struct stat` names a C tag; write `struct c::stat` ``, `` `EOF` is a C macro; write `c::EOF` ``. An import always puts the header's names under `c::`. Inside an attribute's arguments (`[[gnu::format(printf, 1, 2)]]`, `__attribute__((format(printf, 1, 2)))`) a word is the attribute's own and needs no `c::`. Names from `#include`d headers stay bare, as in C, so plain C in a `.smith` file means what it means in a `.c` file. A file with no `module` line lowers nothing else: no prefix, nothing `static`. The sized types (`u32`) are typedefs only in the `.smith` file; the generated header says `uint32_t`, so a C file that includes it has no `u32`. A project's own `typedef uint32_t u32;` in a C header is fine in a `.smith` file that includes it.

A C symbol that no header declares (macOS's `<unistd.h>` has no `environ`), or one a project's C file defines, is declared in the module with `extern` and no `export`: `extern char **environ;`, `extern u32 counter;`, `extern u32 triple(u32 x);`. It keeps its C name (no module prefix, no `__asm__` label), is private to the module and not in its header, and links to the C definition. A header's own variables work with `c::` (`c::optind`, `c::stdout`, `c::errno`). `export extern T name;` is different: the module's own symbol, defined in C by the prefixed name or its `[[smith::c_name]]`.

## smith into C and C++

Each module has a generated header, `.smith/include/game/replay.h`, with `extern "C"` guards and C names bound to the module's symbols:

```c
typedef struct { uint64_t frames; lib_text_Text name; } game_replay_Replay;
game_replay_Replay game_replay_open(const char *path) __asm__("_game::replay::open");
```

C and C++ include it (`-I .smith/include`, with the header in the compile's `.implicit_inputs` in build.c so a clean build writes it first) and call `game_replay_open`; no other change. An export with no body is defined in C or C++ against the header, by its C name (`uint64_t game_replay_checksum(...) { … }`, `extern "C"` in C++).

- An export with `[[smith::c_name("Name")]]` is declared as `Name` in the header and its symbol is `Name` (no `__asm__` label), so existing C code that calls `Name`, or defines it for an export with no body, links unchanged. Other exports keep `game_replay_open` and the `::` symbol.
- `constexpr` exports are in the header as `constexpr` (C23 or C++11); an exported variable is `extern` with the symbol label.
- The header keeps what C needs of the source: the comments directly above an `export` (a doc comment), the comments inside an exported declaration and after its last token on its line, and the `#if`/`#ifdef`/`#elif`/`#else`/`#endif` lines around exports, verbatim (a `.smith` module has none of its own: an exported `inline` function's `if constexpr` on `target::` is written to the header as `#if`, with the `SMITH_TARGET_*` macros it needs defined once at the top). The header is never preprocessed, so the includer's `-D` flags choose the branch; the module is compiled with its own flags. A blank line between a comment and the `export` makes it no doc comment.
- `export inline T f(...) { ... }` is `static inline game_replay_f` in the header with its body, and the module defines it the same way: no symbol, so C, C++ and `.smith` importers each compile it. Its body may use only the module's other exports, its imports and C, never a private name.
- **The header includes what the exports need and nothing else**: `<stddef.h>` and `<stdint.h>`; an imported module (`import smith::arena;`) when an export writes `arena::Name`, included relative to the header (`#include "arena.h"` beside it, `"../lib/text.h"` from another folder), so the headers of one tree find each other with no include path; a C header (`import <stdio.h>; // printf and the like are raw effects: take a capability instead (smith-io)`, `import "mine.h";`) when an export writes a name of it, `c::FILE` or `struct c::stat`. An import only a body uses stays out, so a C file that includes the header does not get stdio. With several C imports smith asks the compiler which one declares the name (a stage0 cannot, and keeps them all). An `#include` of the module never reaches the header: a module whose exports use a header's types `import`s it and writes `c::SmithArena`, not `SmithArena`. A conditional around imports (`#if __has_include("arena.h") import "arena.h"; #else import <smith/arena.h>; #endif`) is kept with them. An exported variable is declared (`extern`, with its label) before its definition in the module's own compile, so `-Wmissing-variable-declarations -Werror` needs no pragma.
- A header is written without macros (a `#define` of the module is not copied): keep a macro out of an export's signature.
- Link C++ with `smith c++`.

## Debugging

`b game::replay::open` in lldb; backtraces show `_game::replay::open`; the source lines and columns are the `.smith` ones.
