---
name: smith-arena
description: "Using <smith/arena.h>, smith's arena for memory without malloc. Load when code includes <smith/arena.h>, when a build.c calls smith_library(b, \"arena\"), or on a question about allocating memory in C without malloc."
---

# Memory without malloc: smith's arena

This skill ships in the smith plugin, from `.agents/skills/smith-arena/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

An arena is a fixed block of memory with a cursor. Nothing is freed on its own: a reset frees everything and a scope frees what was pushed since it began. The header is `.smith/include/smith/arena.h`; read it first.

## Use it

```c
#include <smith/arena.h>

alignas(smith_arena_align_max) static unsigned char memory[1 << 20]; // aligned, so layouts never depend on addresses
SmithArena arena = smith_arena_make(memory, sizeof memory);

Foo *foo = SMITH_ARENA_PUSH(&arena, Foo);                // zeroed
Foo *foos = SMITH_ARENA_PUSH_ARRAY(&arena, Foo, count);  // zeroed, count * size checked for overflow
```

In a `.smith` file the type is an argument (a generic, `smith-lang-generics`), and the result is typed the same way:

```c
import smith::arena;

alignas(arena::align_max) static u8 memory[1 << 20];
arena::Arena a = arena::make(memory, sizeof memory);

Foo *foo = arena::push(&a, Foo);                // zeroed
Foo *foos = arena::push_array(&a, Foo, count);  // zeroed, count * size checked for overflow
```

The module is `smith::arena` (`lib/smith/arena.smith`), and its C API is the generated header: `arena::make`, `fits`, `push_raw` (`smith_arena_push`), `push_array_raw` (`smith_arena_push_array`), `carve`, `reset`, `scope_begin`, `scope_end`, the types `arena::Arena` and `arena::Scope` (`SmithArena`, `SmithArenaScope`) and `arena::align_max`. C and C++ cannot call a generic, so they keep the macros, which `<smith/arena.h>` takes from `<smith/arena_typed.h>`. A `.smith` project needs a smith with clang inside to compile the library.

- Prefer the macros (in `.smith`, `arena::push` and `arena::push_array`): the type is written once, so size and alignment cannot disagree, and `Bar *bar = SMITH_ARENA_PUSH(&arena, Foo);` is a compile error.
- When the type is not known at the call site, use the functions (`arena::push_raw(&a, size, align)` in `.smith`): `smith_arena_push(&arena, size, align)` and `smith_arena_push_array(&arena, size, count, align)`. `align` is a power of two, at most `smith_arena_align_max` (64).
- `smith_arena_fits(&arena, size, align)` asks whether a push would fit.
- A scope gives back what a stretch of work pushed: `SmithArenaScope scope = smith_arena_scope_begin(&arena); ...; smith_arena_scope_end(scope);`. Scopes end in reverse order of beginning, and `smith_arena_reset(&arena)` is allowed only with none open; both are `assert`s.

## Carve arenas out of a reservation

`SmithArena frame = smith_arena_carve(&parent, capacity);` takes the next `capacity` bytes after the parent's cursor (aligned to `smith_arena_align_max`) as an arena of its own, and moves the parent's cursor past them. Nothing is touched or poisoned, so out of a big reservation (the 64 GiB arena of `smith_io_make`) a program carves several arenas with different lifetimes at start, such as one for the run, one per level and one reset every frame, and pages are committed only as each is used. Under AddressSanitizer each carved arena poisons lazily for itself and catches the same overflows and stale reads as any arena. Carving past the parent's capacity is the same stop as a push; a scope open on the parent is an `assert`, since ending it would hand the carved memory back. Do not reset the parent after carving.

## Get the library in build.c

smith writes the header and the source into `.smith/` on every `smith build`. The project compiles the source itself, with its own flags, so sanitizers and warnings match its code:

```c
SmithObject arena = {
    .compiler = "smith-cc",
    .source = smith_library(b, "arena"),
    .output = "build/smith/arena.o",
    .flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr),
};
smith_object(b, &arena);
```

Add `-I .smith/include` to the project's own compiles too, and put `arena` in the program's `.objects`. Nothing is linked unless build.c asks.

## What the stop means

A push past the capacity prints `smith: arena out of capacity: push of N bytes (align A) with U of C bytes used` to stderr and calls `abort()`, in every build, `NDEBUG` included. It is not an assertion: it is the one check that must survive a fast build, so memory is never corrupted silently. The fix is a bigger block or less pushed, not a retry. Under AddressSanitizer, each push starts on an 8-byte granule with a poisoned gap after it, and the 64 KiB past the cursor is poisoned (lazily, so a 64 GiB arena costs no shadow until it is used), so a write just outside a push is caught.
