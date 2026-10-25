---
name: smith-mem
description: "Using smith::mem (import smith::mem; mem::alloc, alloc_slice, try_alloc, set_allocator): memory from the allocator in force on the thread, never null. Load when code imports smith::mem or includes <smith/mem.h>, or on swapping the allocator around code with defer."
---

# Memory from the allocator in force: smith::mem

`mem::alloc(T)` is a zeroed `T *` from the allocator of the calling thread: the system's by default, an arena or a fake after `mem::set_allocator`. It is never null: a refusal (the OS out of memory, a fixed arena's budget) is a checked failure naming the size, which aborts for now (with tasks, #324, it fails the task and unwinds through `defer`). Code that can do something about a refusal calls `mem::try_alloc`, which returns an `option`. `malloc` keeps C's contract (null when refused).

```smith
module scratch;
import smith::arena;
import smith::mem;

export typedef option(i32 *) MaybeInt; // an option type is named at file scope before a body uses it

export i32 or_zero(MaybeInt maybe) { // read an option in a function with no generic call (#353)
    if (maybe == .some) return *maybe.some;
    return 0;
}

export void fill(arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a);        // an arena as an allocator
    mem::Allocator *before = mem::set_allocator(&allocator.base); // returns the one it had
    defer mem::set_allocator(before);                             // restore it on every return
    i32 *one = mem::alloc(i32);                                   // zeroed, from the arena
    f32 grid[:][:] = mem::alloc_slice(f32, 3, 4);                 // one block, row-major
    MaybeInt maybe = mem::try_alloc(i32);                         // .some = p, or .none when refused
    *one = or_zero(maybe) + 2;
    mem::free(one); // the arena takes back its last block only; any other waits for a reset
}
```

- The calls: `mem::alloc(T)`, `mem::alloc_slice(T, sizes...)` (as `arena::push_slice`), `mem::try_alloc(T)`, `mem::free(p)`, `mem::set_allocator(a)` (returns the previous; null is the system's) and `mem::allocator()`. For C and sizes known at run time: `mem::alloc_raw(size, align)` (`smith_mem_alloc`), `alloc_array_raw`, `try_alloc_raw` (null when refused).
- An `Allocator` is a struct whose first member is a `SmithAllocator` (`alloc`, `realloc`, `free`, `owns`, optional `size`, `<smith/handlers.h>`); the setter is per thread. `arena::allocator` makes one over an arena (a growing arena never refuses until its backing does); how `malloc` reaches it is in knowledge/handlers.md. Set the previous one back before the allocator's memory goes away.
- Functions return what they allocate; they do not fill an out parameter (AGENTS.md).
