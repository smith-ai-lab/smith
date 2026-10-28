---
name: smith-lang-c-headers
description: "A smith module seen from C and C++: the generated header (.smith/include/game/replay.h, extern \"C\", game_replay_open and its __asm__ label), what the header keeps (comments, #if lines, export inline), what it includes, calling a module from C or C++ and linking with smith c++. Load when C or C++ calls a smith module, on `smith cc --smith-header`, a generated header that lacks a name or includes too much, or lldb breakpoints on a module's function."
---

# A module from C and C++

```smith
module game::replay;

// How many frames a replay holds.
export typedef struct { U64 frames; } Replay;
export Replay open(U64 frames) { return (Replay){.frames = frames}; }
```

```c
#include "game/replay.h"                  // .smith/include/game/replay.h, written by smith build

int main(void) {
    game_replay_Replay r = game_replay_open(3);   // the module's names, joined with _
    return (int)r.frames - 3;
}
```

The header (`smith cc --smith-header game/replay.smith -o game/replay.h`) has `extern "C"` guards, the doc comment, `typedef struct { uint64_t frames; } game_replay_Replay;` and `game_replay_Replay game_replay_open(uint64_t frames) __asm__("_game::replay::open");`.

## Rules

- C and C++ include it with `-I .smith/include` and call the C names; no other change. In build.c put the header in the compile's `.implicit_inputs` so a clean build writes it first. Link C++ with `smith c++`. `[[std::c_name]]` and exports without a body: `smith-lang-exports`.
- **The header includes what the exports need and nothing else**: `<stddef.h>` and `<stdint.h>`; an imported module (`import std::arena;`) when an export writes `arena::Name`, relative to the header (`#include "arena.h"` beside it), so a tree of headers finds itself with no include path; a C header (`import <stdio.h>;`, `import "mine.h";`) when an export writes a name of it (`c::FILE`, `struct c::stat`). An import only a body uses stays out, so a C file that includes the header does not get stdio. With several C imports smith asks the compiler which declares the name (a stage0 cannot, and keeps them all). An `#include` of the module never reaches the header: a module whose exports use a header's types `import`s it and writes `c::SmithArena`. A conditional around imports (`#if __has_include("arena.h") import "arena.h"; #else ...`) is kept with them.
- **It keeps what C needs of the source**: the comments directly above an `export` (a blank line makes it no doc comment), the comments inside an exported declaration and after its last token on its line, and the `#if`...`#endif` lines around exports, verbatim. The header is never preprocessed, so the includer's `-D` flags choose the branch; the module is compiled with its own flags. An exported `inline` function's `if constexpr` on `target::` is written as `#if`, with the `SMITH_TARGET_*` macros defined once at the top.
- `constexpr` exports are `constexpr` in the header (C23 or C++11); an exported variable is `extern` with its symbol label, declared before its definition in the module's own compile, so `-Wmissing-variable-declarations -Werror` needs no pragma.
- `export inline T f(...)` is `static inline game_replay_f` with its body. Attributes after `export` stay in front of `static` (`[[std::c_name("x_f")]] export [[gnu::always_inline, maybe_unused]] inline void f()`), so a function nothing calls is quiet under `-Wunused-function`.
- A header is written without macros (a module's `#define` is not copied): keep a macro out of an export's signature.
- A generic is not callable from C (a `// smith-generic:` line serves importing modules): give C a typed wrapper with `[[std::c_name]]`.

## Debugging

`b game::replay::open` in lldb; backtraces show `_game::replay::open`; source lines and columns are the `.smith` ones.
