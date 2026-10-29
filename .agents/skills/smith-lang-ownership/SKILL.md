---
name: smith-lang-ownership
description: "What a returned pointer or slice means in .smith modules and C: owned (freed by a function), borrowed from a parameter, internal, or the caller's context, inferred from the body and written to the .fx summary, or stated with [[std::returns_owned(f)]], [[std::returns_borrowed(n)]], [[std::returns_internal]]. Load on 'smith-ownership:', 'returns different things on different paths', 'origin smith cannot read', 'says [[std::returns_', or when a function returns a pointer."
---

# Ownership idioms

A pointer a function returns follows one of C's four idioms, and smith reads which from the body, as it reads effects ([smith-lang-effects](../smith-lang-effects/SKILL.md)):

| idiom | the summary says | what the body returns |
| --- | --- | --- |
| owned | `owned:free` | the result of an owned-returning function, unfreed: `c::strdup(t)`, `c::fopen(...)` (`owned:fclose`), a function already owned |
| borrowed | `borrowed:2` | a pointer derived from parameter 2 (1-based): the parameter, `&p->field`, `&p[i]`, `p + k`, a sub-slice `s[a:b]`, or what a borrowed-returning function returns for it |
| internal | `internal` | a static or global, a string literal, `c::getenv(...)`, a field of a global or a `constexpr` |
| the caller's context | `context` | `mem::alloc*` and `arena::push*` (the current allocator), or a function that returns only those; smith code's default, no attribute |

```smith
export typeof(u8[:]) to_upper(typeof(const u8[:]) s) { /* mem::alloc_slice */ }   // context
export typeof(const u8[:]) trim(typeof(const u8[:]) s) { return s[1:countof(s)]; } // borrowed:1
export char *copy(const char *t) { return c::strdup(t); }                          // owned:free
export const char *home() { return c::getenv("HOME"); }                            // internal
```

Write nothing: `smith cc -c` infers each, and the summary (`.smith/effects/...fx`, `O <symbol> <idiom> <place>`) records it. A callee in another file is read at the link from its summary (`R` lines wait for it); `<program>.owns` lists what only the link settled. A libc function's idiom is its row in `shim/effects_table.h` (`owned:munmap:2` also passes argument 2 to the free function); `getcwd(nullptr, 0)` is owned, `getcwd(buf, n)` borrowed, and `getcwd(p, n)` with an unknown `p` is an error asking for an array or `nullptr`. A system-header function that returns a pointer and has no row is `error: smith-ownership: 'f' returns a pointer and the C library table gives it no idiom`; add the row.

## Errors (in a module; a `.c` file is inferred for and never refused)

- **Mixed paths**: `strdup(a)` on one path and `a` on another is `smith-ownership: 'f' returns different things on different paths: owned, freed by 'free' (a.smith:8:15) and borrowed from parameter 1 (...)`; two owned paths freed by different functions the same. Make every path one kind, then write the attribute that names it. A longer-lived path gives way to the shortest: `borrowed:1` with `internal` (a literal) is `borrowed:1`, and `internal` with `context` is `internal`.
- **An origin smith cannot read**: a pointer loaded from a field (`return loop->fake;`), through a pointer, from a function pointer, a local array, or a variable assigned from such: `... returns a pointer whose origin smith cannot read (...); write [[std::returns_owned(f)]], [[std::returns_borrowed(n)]] or [[std::returns_internal]] on it`. Never a guess: write the attribute. An attribute on a pointer smith cannot read stands.
- **An attribute the body contradicts**: `says [[std::returns_borrowed(1)]] but returns a pointer that is owned, freed by 'free'`, in C too.
- **At the link**: the same errors for a function that waited on another file (`c.c:3:57: error: smith-ownership: ...`).

## Rules and limits

- The attributes: `[[std::returns_owned(f)]]` (`f` a declared function of one pointer), `returns_borrowed(n)` (`n` a pointer or slice parameter), `returns_internal`; one per declaration, on a function returning a pointer or a slice. There is none for the caller's context, nor on a generic function (the lowering reads it as no generic): an instance is inferred and never an error.
- A struct returned by value has no idiom and no error; a `contract::trusted()` function is inferred where it can be read and never an error.
- Nothing is judged at a call site yet (regions #439 and linear types #438 will use the summary).
- Count them: `smith build ownership [-Dproject=<folder>]` prints the functions per idiom, those that wait on another file and those rejected, with the messages.

Reference: [knowledge/ownership-idioms.md](../../../knowledge/ownership-idioms.md).
