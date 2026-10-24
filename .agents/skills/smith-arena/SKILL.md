---
name: smith-arena
description: "Using smith::arena (import smith::arena; arena::push, push_array): memory without malloc. Load when code imports smith::arena or includes <smith/arena.h>, or on allocating memory in C without malloc."
---

# Memory without malloc: smith's arena

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

i32 main(void) {
    alignas(arena::align_max) static u8 memory[1 << 20]; // aligned, so layouts never depend on addresses
    arena::Arena a = arena::make(memory, sizeof memory);
    Point *origin = arena::push(&a, Point); // zeroed
    Point *line = make_line(&a, 10);
    return origin->x == 0 && line[3].x == 3 ? 0 : 1;
}
```

In a program the arena is `io.arena` (`smith-io`), made in `main` and passed down; the static block above is for a program without io.

- `arena::push(&a, T)` (a `T *`), `arena::push_array(&a, T, count)` (a `T *`) and `arena::push_slice(&a, T, count)` (a `T[:]`: the pointer and the count together, `Point line[:] = arena::push_slice(&a, Point, 10);`, zeroed, `countof(line)` is 10; bind it before indexing it) are the calls to write. Never `arena::push_raw(&a, sizeof(T), alignof(T))`: that is the C API in `.smith`.
- The module's names: `arena::make`, `fits`, `push`, `push_array`, `push_slice`, `push_raw` (`smith_arena_push`), `push_array_raw` (`smith_arena_push_array`), `carve`, `reset`, `scope_begin`, `scope_end`, the types `arena::Arena` and `arena::Scope` (`SmithArena`, `SmithArenaScope`) and `arena::align_max`. A `.smith` project needs a smith with clang inside to compile the library.
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

Also in this family: [smith-arena-c](../smith-arena-c/SKILL.md).
