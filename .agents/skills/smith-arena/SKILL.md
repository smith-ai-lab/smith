---
name: smith-arena
description: "Using smith::arena (import smith::arena; arena::push, push_slice): memory without malloc. Load when code imports smith::arena or includes <smith/arena.h>, or on allocating memory in C without malloc."
---

# Memory without malloc: smith's arena

An arena is a block of memory with a cursor. Nothing is freed on its own: a reset frees everything and a scope frees what was pushed since it began. A **growing** arena takes another chunk from its backing when full, so a push never fails; a **fixed** arena never grows. The header is `.smith/include/smith/arena.h`; read it first.

## Use it

In a `.smith` file the arena is a module (`lib/smith/arena.smith`, `module smith::arena;`) and the type is an argument, so size and alignment cannot disagree and the result is typed (`smith-lang-generics`):

```smith
import smith::arena;

typedef struct {
    f32 x, y;
} Point;

Point *make_line(arena::Arena *a, usize count) {
    Point *points = arena::push_slice(a, Point, count).data; // zeroed, count * size checked for overflow
    for (usize i = 0; i < count; i++) points[i] = (Point){.x = (f32)i, .y = 0};
    return points;
}

i32 main() {
    alignas(arena::align_max) static u8 memory[1 << 20]; // aligned, so layouts never depend on addresses
    arena::Arena a = arena::fixed(memory, sizeof memory); // never grows
    Point *origin = arena::push(&a, Point); // zeroed
    Point *line = make_line(&a, 10);
    return origin->x == 0 && line[3].x == 3 ? 0 : 1;
}
```

In a program the arena is `io.arena` (`smith-io`), made in `main` and passed down; the static block above is for a program without io.

- `arena::push(&a, T)` (a `T *`) and `arena::push_slice(&a, T, sizes...)` (a slice, with a size for each `[:]` of the result: `Point line[:] = arena::push_slice(&a, Point, 10);`, `f32 grid[:][:] = arena::push_slice(&a, f32, rows, cols);`, `f32 rows[:][2] = arena::push_slice(&a, f32[2], count);`, zeroed, one contiguous block, `countof(line)` is 10; bind it before indexing it; its type is `arena::slice_of(T, countof(sizes))`; `.data` is the pointer for a C function) are the calls to write. There is no `arena::push_array`. Never `arena::push_raw(&a, sizeof(T), alignof(T))`: that is the C API in `.smith`.
- **Growing:** `arena::Arena a = arena::growing(1 << 20, nullptr);` (first chunk size, backing: the OS, or `&allocator.base` of a `smith_handlers.h` allocator) takes a chunk of twice the size when full. `push` and `push_slice` never return null; `a.grown` is the bytes beyond the first chunk. Chunks stay across `reset` and `scope_end`, so a second pass over the same work takes none (zeroing is the cost); `arena::release(&a)` gives them all back. The OS or the backing refusing is a checked failure naming the size; a **fixed** arena that is full is a checked failure naming the capacity (it aborts, in every build). `arena::try_push_raw(&a, size, align)` and `try_push_array_raw` return null instead, for code that refuses; in `.smith` write `arena::try_push(&a, T)` (an `option(T *)`), `.none` when a fixed arena is full or the backing refuses, with nothing pushed (a slice has no `option` form yet: `option` takes a type name, issue #353) (`smith-option`; C uses `try_push_raw`).
- The module's names: `arena::fixed`, `growing`, `release`, `try_push_raw`, `try_push`, `fits`, `push`, `push_slice`, `slice_of`, `push_raw` (`smith_arena_push`), `push_array_raw` (`smith_arena_push_array`), `carve`, `reset`, `scope_begin`, `scope_end`, the types `arena::Arena` and `arena::Scope` (`SmithArena`, `SmithArenaScope`) and `arena::align_max`. A `.smith` project needs a smith with clang inside to compile the library. `arena::fits(&a, size, align)` asks whether a push would fit (the current chunk of a growing one); `align` of the raw calls is a power of two, at most `arena::align_max` (64). A function that needs memory takes an arena, never mallocs.
- A scope gives back what a stretch of work pushed, and scopes end in reverse order of beginning. End it with `defer` right after it begins, so every return gives the memory back (`smith-lang-defer`):

```smith
module scratch;
import smith::arena;

export usize scratch_sum(arena::Arena *a, usize count) {
    arena::Scope scope = arena::scope_begin(a);
    defer arena::scope_end(scope); // items is given back on every return
    usize items[:] = arena::push_slice(a, usize, count);
    usize sum = 0;
    for (usize i = 0; i < count; i++) {
        items[i] = i;
        sum += items[i];
    }
    return sum;
}
```

`arena::reset(&a)` is allowed only with no scope open; both are `assert`s. Also in this family: [smith-arena-c](../smith-arena-c/SKILL.md).
