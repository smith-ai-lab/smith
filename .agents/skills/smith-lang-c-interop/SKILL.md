---
name: smith-lang-c-interop
description: "C and C++ with .smith: `import <math.h>;`, `c::printf`, `c::EOF`, `c::WIFEXITED(s)`, generated C headers (.smith/include, game_replay_open), calling a smith module from C or C++, an export with no body defined in C. Load on 'is a C name; write c::', 'is a C macro', 'names a C tag', `#include` in a module, `__asm__` labels, or extern \"C\" against a module."
---

# C and C++ beside `.smith`

This skill ships in the smith plugin, from `.agents/skills/smith-lang-c-interop/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`.

## C headers into smith

`import <h>;` (or `import "h";`, looked for beside the file first) includes the header and binds `c`: `c::sqrtf(x)`, `c::EOF`, `c::WIFEXITED(s)`, `struct c::stat`. Macros work. `c::` names C only in a file that imports a header; otherwise `c` is any identifier.

In a module, a name an imported header declared, used without `c::`, is an error (a function, variable, type, tag or macro): `` `sqrtf` is a C name; write `c::sqrtf` ``. Names from `#include`d headers stay bare, as in C. In a file that is not a module, nothing is checked.

## smith into C and C++

Each module has a generated header, `.smith/include/game/replay.h`, with `extern "C"` guards and C names bound to the module's symbols:

```c
typedef struct { uint64_t frames; lib_text_Text name; } game_replay_Replay;
game_replay_Replay game_replay_open(const char *path) __asm__("_game::replay::open");
```

C and C++ include it (`-I .smith/include`) and call `game_replay_open`; no other change. An export with no body is defined in C or C++ against the header, by its C name (`uint64_t game_replay_checksum(...) { … }`, `extern "C"` in C++).

- `constexpr` exports are in the header as `constexpr` (C23 or C++11); an exported variable is `extern` with the symbol label.
- A header is written without macros or `#if` (only `#if 0` is honoured): keep an export out of a macro.
- Link C++ with `smith c++`.

## Debugging

`b game::replay::open` in lldb; backtraces show `_game::replay::open`; the source lines and columns are the `.smith` ones.
