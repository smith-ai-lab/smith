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
    *one = or_zero(maybe) + 2; // mem::free(one) gives back the arena's last block only
}
```

- The calls: `mem::alloc(T)`, `mem::alloc_slice(T, sizes...)` (as `arena::push_slice`), `mem::try_alloc(T)`, `mem::free(p)`, `mem::set_allocator(a)` (returns the previous; null is the system's) and `mem::allocator()`. For C and sizes known at run time: `mem::alloc_raw(size, align)` (`smith_mem_alloc`), `alloc_array_raw`, `try_alloc_raw` (null when refused).
- An `Allocator` is a struct whose first member is a `SmithAllocator` (`alloc`, `realloc`, `free`, `owns`, optional `size`, `<smith/handlers.h>`); the setter is per thread. `arena::allocator` makes one over an arena (a growing arena never refuses until its backing does); how `malloc` reaches it is in knowledge/handlers.md. 

## Finding mistakes: mem::checked
`mem::checked(parent)` (`smith_mem_checked`; a `null` parent is the system's), used as `set_allocator(&c.base)`, then `checked_report(&c)` and `checked_release(&c)` when done, wraps an allocator: each block records the stack of its allocation, `mem::free` of a block it already freed (a double free) or never made stops with `memory error` naming both sites, a freed block is kept and poisoned (ASan: `use-after-poison`), and a leak is listed with where it was allocated. `smith test` sets one around every test: a test that returns with a live block fails with `memory leaked` at the line that allocated it (`--unchecked` for a run that must not). It costs nothing in a program that never makes one.

`checked_leaks(&c)` (count, bytes and first site of the leaks), `checked_leak(&c, after)` (the next leak's site, in order of allocation), `checked_live`, `checked_live_bytes`. A block used after an arena's `reset` is `use-after-poison` already: the arena poisons what is past its cursor. Details: knowledge/checked-allocator.md.

## Refusing on purpose
The allocator that refuses by chance is its own module, [smith-failing](../smith-failing/SKILL.md): `failing::make(parent, key)`, built on `random`, over this interface.

## Copies, fills and comparisons: typed, in place of memcpy and friends
```smith
module copies;
import smith::mem;

export bool demo(u8 dst[:], const u8 src[:]) {
    mem::copy(dst, src);                    // two slices of one element type and one length: unequal lengths fail a check
    u32 word = 0;
    mem::zero(mem::bytes(&word));           // mem::bytes(&x) is the value's own u8[:]; mem::fill(xs, v) sets each element
    return mem::equal(dst[0:2], src[0:2]);  // the lengths are compared first, then the bytes
}
```

- `mem::copy(dst, src)`, `mem::move(dst, src)` (overlap allowed), `mem::zero(xs)` or `mem::zero(&x)`, `mem::fill(xs, v)`, `mem::equal(a, b)` (a `bool`; bytes, so a NaN equals itself and `0.0` differs from `-0.0`), `mem::bytes(&x)` (`u8[:]`; `mem::read_bytes(&x)` is `const u8[:]`). An element type that differs is a compile error; a length that differs is a checked failure in every build. They lower to the C functions: no cost beyond the checks.
- Bytes become a typed slice in one place: `mem::can_view(raw, f32)` asks (a whole number of elements, aligned), `mem::view(raw, f32)` returns `f32[:]` and is a checked failure when `can_view` is false (`raw` is a `u8[:]`; an `option` of a slice cannot be written in a generic yet, #408).
- **In a file with a `module` line the raw functions are a `smith-mem` finding** (`c::memcpy`, `c::memmove`, `c::memset`, `c::memcmp`, `__builtin_memcpy` and the others): the message names the replacement, and `smith lint --fix` rewrites `memset(&x, 0, sizeof x)` to `mem::zero(&x)`. A function that implements a wrapper over the raw function says `contract::trusted()` (`smith_trusted()` in C) and the check skips it. A `.c` file, or a `.smith` file with no `module` line, is untouched (smith-check-lints).
- **`mem::alloc_uninit(T)`** (`alloc_uninit_raw`, `smith_mem_alloc_uninit` from C) is the opt-out of zeroed memory, as `arena::push_uninit`: only for a measured hot spot (more than 2%), with the measurement in a comment at the call.
