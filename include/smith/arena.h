// <smith/arena.h>: a fixed block of memory with a cursor, for programs that do not call malloc. Nothing is freed on its
// own; a reset frees everything, and a scope frees what was pushed since it began.
//
// The project compiles lib/arena.c itself (build.c: smith_library(b, "arena")), so the library is built with the
// project's own flags, sanitizers included.
//
// Typed pushes are two one-line macros over the functions, which stay the real API:
//   Foo *foo = SMITH_ARENA_PUSH(arena, Foo);
//   Foo *foos = SMITH_ARENA_PUSH_ARRAY(arena, Foo, count);
// They are macros because the function form is not equally good: the type is written once, so the size and the
// alignment cannot disagree, and the result is typed, so `Bar *bar = SMITH_ARENA_PUSH(arena, Foo);` is a compile error
// where the function's void * converts silently. First-class types could remove them one day. When the type is not
// known at the call site, use the functions:
//   void *item = smith_arena_push(arena, size, align);
#pragma once

#include <stddef.h>

// The alignment an arena's memory must start at, and the largest a push may ask for. Pushes then lay out the same way
// whatever the addresses, so a run is deterministic.
constexpr size_t smith_arena_align_max = 64;

typedef struct {
    unsigned char *memory;
    size_t capacity;
    size_t used;
    size_t scopes;  // scopes begun and not yet ended
    size_t touched; // AddressSanitizer builds: the poison high-water mark, see lib/arena.c; otherwise unused
} SmithArena;

// A scope: ending it returns the cursor to where it began. Scopes end in the reverse order they began, and an arena is
// reset only with none open; both are asserted.
typedef struct {
    SmithArena *arena;
    size_t used;
    size_t depth; // the arena's scopes before this one began
} SmithArenaScope;

#define SMITH_ARENA_PUSH(arena, Type) ((Type *)smith_arena_push((arena), sizeof(Type), alignof(Type)))
#define SMITH_ARENA_PUSH_ARRAY(arena, Type, count) \
    ((Type *)smith_arena_push_array((arena), sizeof(Type), (count), alignof(Type)))

// memory must be aligned to smith_arena_align_max and hold capacity bytes.
[[nodiscard]] SmithArena smith_arena_make(void *memory, size_t capacity);

// Zeroed memory for size bytes at align (a power of two, at most smith_arena_align_max). Running out of capacity stops
// the program with a message naming the request and the capacity, in every build: it is a single comparison, and
// without it a build with NDEBUG would write past the memory.
[[nodiscard]] void *smith_arena_push(SmithArena *arena, size_t size, size_t align);

// count items of size bytes; an overflowing count * size is the same stop.
[[nodiscard]] void *smith_arena_push_array(SmithArena *arena, size_t size, size_t count, size_t align);

// Whether a push of size at align would fit.
[[nodiscard]] bool smith_arena_fits(const SmithArena *arena, size_t size, size_t align);

// A second arena of capacity bytes out of the memory the parent has not used: the next capacity bytes after its cursor,
// aligned to smith_arena_align_max, and the parent's cursor moves past them. Nothing in them is touched or poisoned, so
// when the parent is a reservation of address space (the 64 GiB mmap of smith_io_make) the carved arena commits pages
// only as it is used, and a program can carve hundreds of MiB at start for arenas with different lifetimes (one reset
// every frame, one for the run). Running out of the parent's capacity is the same stop as a push. Asserts that the
// parent has no scope open, since the end of the scope would hand the carved memory back.
[[nodiscard]] SmithArena smith_arena_carve(SmithArena *parent, size_t capacity);

// Frees everything. Asserts no scope is open.
void smith_arena_reset(SmithArena *arena);

[[nodiscard]] SmithArenaScope smith_arena_scope_begin(SmithArena *arena);
void smith_arena_scope_end(SmithArenaScope scope);
