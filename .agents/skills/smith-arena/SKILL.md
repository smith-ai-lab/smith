---
name: smith-arena
description: "Using std::arena (import std::arena; arena::push, push_slice): memory without malloc. Load when code imports std::arena or includes <smith/arena.h>, or on allocating memory in C without malloc."
---

# Memory without malloc: smith's arena

An arena is a block of memory with a cursor. Nothing is freed on its own: a reset frees everything and a scope frees what was pushed since it began. A **growing** arena takes another chunk from its backing when full, so a push never fails; a **fixed** arena never grows. The header is `.smith/include/smith/arena.h`; read it first.

## Use it

In a `.smith` file the arena is a module (`lib/std/arena.smith`, `module std::arena;`) and the type is an argument, so size and alignment cannot disagree and the result is typed (`smith-lang-generics`):

```smith
module points;
import std::arena;

typedef struct {
    F32 x, y;
} Point;

Point *make_line(arena::Arena *a, Usize count) {
    Point *points = arena::push_slice(a, Point, count).data; // zeroed, count * size checked for overflow
    for (Usize i = 0; i < count; i++) points[i] = (Point){.x = (F32)i, .y = 0};
    return points;
}

I32 main() {
    arena::Arena a = {}; // the zero value grows from the current allocator at its first push
    defer arena::release(&a);
    Point *origin = arena::push(&a, Point); // zeroed
    Point *line = make_line(&a, 10);
    return origin->x == 0 && line[3].x == 3 ? 0 : 1;
}
```

- `arena::push(&a, T)` (a `T *`) and `arena::push_slice(&a, T, sizes...)` (a slice, with a size for each `[:]` of the result: `Point line[:] = arena::push_slice(&a, Point, 10);`, `F32 grid[:][:] = arena::push_slice(&a, F32, rows, cols);`, `F32 rows[:][2] = arena::push_slice(&a, F32[2], count);`, zeroed, one contiguous block, `countof(line)` is 10; bind it before indexing it; its type is `arena::slice_of(T, countof(sizes))`; `.data` is the pointer for a C function) are the calls to write. There is no `arena::push_array`. Never `arena::push_raw(&a, sizeof(T), alignof(T))`: that is the C API in `.smith`.
- **Growing:** `arena::Arena a = {}; defer arena::release(&a);` (the zero value; also a zeroed struct field) takes its first chunk at its first push, from the current allocator (a test's checker reports a missing `release` as `memory leaked`); `arena::growing(1 << 20, nullptr)` takes the first chunk now (first chunk size, backing: the current allocator, or `&allocator.base` of a `smith_handlers.h` allocator). Either takes a chunk of twice the size when full. `push` and `push_slice` never return null; `a.grown` is the bytes beyond the first chunk. Chunks stay across `reset` and `scope_end`, so a second pass over the same work takes none (zeroing is the cost); `arena::release(&a)` gives them all back. The backing refusing is a checked failure naming the size; a **fixed** arena that is full is a checked failure naming the capacity (it aborts, in every build). `arena::try_push_raw(&a, size, align)` and `try_push_array_raw` return null instead, for code that refuses; in `.smith` write `arena::try_push(&a, T)` (an `Option(T *)`), `.none` when a fixed arena is full or the backing refuses, with nothing pushed ; `arena::try_push_slice(&a, T, sizes...)` is the same for a slice, an `option` of `arena::slice_of(T, countof(sizes))` (`.none` also when the sizes overflow; nothing pushed); bind it in a typedef'd `option` first (`smith-option`; C uses `try_push_raw`).
- The module's names: `arena::fixed`, `growing`, `release`, `try_push_raw`, `try_push`, `try_push_slice`, `fits`, `push`, `push_slice`, `slice_of`, `push_raw` (`smith_arena_push`), `push_array_raw` (`smith_arena_push_array`), `carve`, `reset`, `scope_begin`, `scope_end`, the types `arena::Arena` and `arena::Scope` (`SmithArena`, `SmithArenaScope`) and `arena::align_max`. A `.smith` project needs a smith with clang inside to compile the library. `arena::fits(&a, size, align)` asks whether a push would fit (the current chunk of a growing one); `align` of the raw calls is a power of two, at most `arena::align_max` (64). A function that needs memory takes an arena, never mallocs.
- A scope gives back what a stretch of work pushed, and scopes end in reverse order of beginning. End it with `defer` right after it begins, so every return gives the memory back (`smith-lang-defer`):

```smith
module scratch;
import std::arena;

export Usize scratch_sum(arena::Arena *a, Usize count) {
    arena::Scope scope = arena::scope_begin(a);
    defer arena::scope_end(scope); // items is given back on every return
    Usize items[:] = arena::push_slice(a, Usize, count);
    Usize sum = 0;
    for (Usize i = 0; i < count; i++) {
        items[i] = i;
        sum += items[i];
    }
    return sum;
}
```

`arena::reset(&a)` is allowed only with no scope open; both are `assert`s. Also in this family: [smith-arena-c](../smith-arena-c/SKILL.md).
