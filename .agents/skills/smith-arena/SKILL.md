---
name: smith-arena
description: "Using <smith/arena.h>, smith's arena for memory without malloc. Load when code includes <smith/arena.h>, when a build.c calls smith_library(b, \"arena\"), or on a question about allocating memory in C without malloc."
---

# Memory without malloc: smith's arena

This skill ships in the smith plugin, from `.agents/skills/smith-arena/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

An arena is a fixed block of memory with a cursor. Nothing is freed on its own: a reset frees everything and a scope frees what was pushed since it began. The header is `.smith/include/smith/arena.h`; read it first.

## Use it

In a `.smith` file the arena is a module (`lib/smith/arena.smith`, `module smith::arena;`) and the type is an argument, so size and alignment cannot disagree and the result is typed (`smith-lang-generics`):

```smith
import smith::arena;

typedef struct {
    f32 x, y;
} Point;

// Memory is passed in, never malloc'd: a function that needs some takes an arena.
Point *make_line(arena::Arena *a, usize count) {
    Point *points = arena::push_array(a, Point, count); // zeroed, count * size checked for overflow
    for (usize i = 0; i < count; i++) points[i] = (Point){.x = (f32)i, .y = 0};
    return points;
}

int main(void) {
    alignas(arena::align_max) static u8 memory[1 << 20]; // aligned, so layouts never depend on addresses
    arena::Arena a = arena::make(memory, sizeof memory);
    Point *origin = arena::push(&a, Point); // zeroed
    Point *line = make_line(&a, 10);
    return origin->x == 0 && line[3].x == 3 ? 0 : 1;
}
```

In a program the arena is `io.arena` (`smith-io`), made in `main` and passed down; the static block above is for a program without io.

- `arena::push(&a, T)` and `arena::push_array(&a, T, count)` are the calls to write. Never `arena::push_raw(&a, sizeof(T), alignof(T))`: that is the C API in `.smith`.
- The module's names: `arena::make`, `fits`, `push`, `push_array`, `push_raw` (`smith_arena_push`), `push_array_raw` (`smith_arena_push_array`), `carve`, `reset`, `scope_begin`, `scope_end`, the types `arena::Arena` and `arena::Scope` (`SmithArena`, `SmithArenaScope`) and `arena::align_max`. A `.smith` project needs a smith with clang inside to compile the library.
- `arena::fits(&a, size, align)` asks whether a push would fit. `align` of the raw calls is a power of two, at most `arena::align_max` (64).
- A scope gives back what a stretch of work pushed, and scopes end in reverse order of beginning:

```smith
import smith::arena;

usize scratch_sum(arena::Arena *a, usize count) {
    arena::Scope scope = arena::scope_begin(a);
    usize *items = arena::push_array(a, usize, count);
    usize sum = 0;
    for (usize i = 0; i < count; i++) {
        items[i] = i;
        sum += items[i];
    }
    arena::scope_end(scope); // items is given back
    return sum;
}
```

`arena::reset(&a)` is allowed only with no scope open; both are `assert`s.

## From C

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

`arena::Arena frame = arena::carve(&parent, capacity);` (C: `SmithArena frame = smith_arena_carve(&parent, capacity);`) takes the next `capacity` bytes after the parent's cursor (aligned to `smith_arena_align_max`) as an arena of its own, and moves the parent's cursor past them. Nothing is touched or poisoned, so out of a big reservation (the 64 GiB arena of `smith_io_make`) a program carves several arenas with different lifetimes at start, such as one for the run, one per level and one reset every frame, and pages are committed only as each is used. Under AddressSanitizer each carved arena poisons lazily for itself and catches the same overflows and stale reads as any arena. Carving past the parent's capacity is the same stop as a push; a scope open on the parent is an `assert`, since ending it would hand the carved memory back. Do not reset the parent after carving.

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
