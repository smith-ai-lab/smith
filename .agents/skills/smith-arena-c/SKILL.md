---
name: smith-arena-c
description: "smith::arena from C (#include <smith/arena.h>, smith_arena_push with sizeof and alignof), carving arenas out of a reservation, compiling it in build.c with smith_library(b, "arena"), and what a stop means. Load when C code includes <smith/arena.h> or an arena stops with a failure report."
---

# arena from C

C and C++ cannot call a generic, so they pass `sizeof` and `alignof` to the functions that return a `void *` (converted with no cast in C). The header is `.smith/include/smith/arena.h`; smith's libraries define no macros.

```c
#include <smith/arena.h>

alignas(smith_arena_align_max) static unsigned char memory[1 << 20];
SmithArena arena = smith_arena_make(memory, sizeof memory);

Foo *foo = smith_arena_push(&arena, sizeof(Foo), alignof(Foo));                  // zeroed
Foo *foos = smith_arena_push_array(&arena, sizeof(Foo), count, alignof(Foo));    // zeroed, count * size checked for overflow
```

Write `sizeof(T)` and `alignof(T)` from the same `T`. `smith_arena_fits(&arena, size, align)` asks whether a push would fit; a scope is `SmithArenaScope scope = smith_arena_scope_begin(&arena); ...; smith_arena_scope_end(scope);` and `smith_arena_reset(&arena)`.

## Carve arenas out of a reservation

`arena::Arena frame = arena::carve(&parent, capacity);` (C: `SmithArena frame = smith_arena_carve(&parent, capacity);`) takes the next `capacity` bytes after the parent's cursor (aligned to `smith_arena_align_max`) as an arena of its own, and moves the parent's cursor past them. Nothing is touched or poisoned, so out of a big reservation (the 64 GiB arena of `io::make`, `smith_io_make` in C) a program carves several arenas with different lifetimes at start, such as one for the run, one per level and one reset every frame, and pages are committed only as each is used. Under AddressSanitizer each carved arena poisons lazily for itself and catches the same overflows and stale reads as any arena. Carving past the parent's capacity is the same stop as a push; a scope open on the parent is an `assert`, since ending it would hand the carved memory back. Do not reset the parent after carving.

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

A push past the capacity is a failure like a failed contract (`smith_failure_format` of `<smith/trace.h>`, so the arena links the trace library, which `smith_program` and `smith_tests` bring with it): it writes `smith-trace begin arena out of capacity`, the line `smith-trace detail push of N bytes (align A) with U of C bytes used` and the stack to stderr, and calls `abort()`, in every build, `NDEBUG` included; `smith symbolize` turns it into the failing line and the call chain. It is not an assertion: it is the one check that must survive a fast build, so memory is never corrupted silently. The fix is a bigger block or less pushed, not a retry. Under AddressSanitizer, each push starts on an 8-byte granule with a poisoned gap after it, and the 64 KiB past the cursor is poisoned (lazily, so a 64 GiB arena costs no shadow until it is used), so a write just outside a push is caught.

Also in this family: [smith-arena](../smith-arena/SKILL.md).
