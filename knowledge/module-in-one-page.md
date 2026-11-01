---
type: design
title: A module in one page
description: What a module rejects, what it offers instead, and the memory, error and concurrency models, with the status of each row (built or planned); the design of a module as a whole, kept current by the lead.
tags: [module, design, memory, errors, concurrency]
---
# A module in one page

The design of a smith module is spread across about twenty issues. This page is the index of the whole, so an agent briefed with one issue sees how it fits (owner, 2026-10-26: "Our documentation needs to be world class"). Read it before designing or building a module rule. A decision that changes a row changes this page in the same turn, and the issue that owned the old decision gets a comment. When a row is built, its concept and skill describe it and the row says built.

## A module in one paragraph

A `.smith` file with a `module` line is C with the footguns removed. Each footgun is a C form the module rejects, with a compile error that names one replacement. The replacement keeps together two things C lets drift apart (a pointer and its length, a tag and its union, a value and its error), and it lowers to plain C. What can't be decided at compile time is a check in every build, and a proof from `smith prove` removes it. A function that calls `smith_trusted()` is the one way out, and `smith lint --trusted` lists every such site. Valid C without a `module` line keeps its C meaning: a module may reject C, never change it.

## Names in scope

A name is in scope in a module with no import exactly when the compiler depends on it: `Option(T)` and `Result(T, E)` (conversions to `.some` and `.ok`, `try`, `errdefer`, and their zero values). Everything else is a library and is imported, `list(T)` included (owner, 2026-10-26: "list is purely for opt-in"). The library's modules are `std::` (#366).

## Memory

Memory comes from the current allocator, and the current allocator is a region: an arena scope, a block that installs its own allocator, or the program's default. Plain `mem::alloc` memory is never freed one object at a time; its region goes all at once, and libraries never free and never ask the caller to. The rare object with a lifetime of its own is `owned(T)` (`mem::alloc_owned(T)`, ended by `mem::free`), and a buffer that grows is `list(T)`; both are linear, so a missed or doubled free is a compile error. The compiler infers regions with no annotations: a pointer can't outlive its region, and a push to a list ends every borrow into it. Everything starts at zero, and every library type's zero is useful (#415). Allocation never returns null: running out is a task failure, and `try_` functions return `option` for code that refuses.

## Errors

An error the caller can act on is a `result` or an `option`, passed up with `try`. A bug or exhaustion (an index out of range, a broken contract, a failed `assert`, a full fixed arena, overflow) is a task failure (#324): the task unwinds through every `defer`, its group is cancelled, and the report names the call stack. Every point that can fail is the inferred effect `fails` (#325), and a proof removes it.

## Concurrency

Work runs in task groups (`task::Group`, `defer task::wait`), and a task receives only values that can't race (#436): pointer-free data, a moved `owned(T)`, a frozen value, a `sync::` handle, or a stream moved at spawn. A module has no mutable globals. Shared mutable state is a `sync::Mutex`, used through `sync::with`, whose lambda may neither wait nor fail, and several locks are taken in one call in address order, so lock deadlock can't be written. Data flows between tasks through generators (`stream::Stream(T)` and `yield`), not channels. A task never blocks its worker.

## What a module rejects, and what it offers instead

Built means in the language today; planned means decided and in an open issue.

| C form | In a module, instead | Issue | Status |
|---|---|---|---|
| a pointer and a length | a slice, `T xs[:]`, checked on every index | slices | built |
| `int`, `long`, `char` for numbers | sized types, `I32`, `U8`, `Usize` | sized types | built |
| the preprocessor | modules, `target::`, `if constexpr` | preprocessor check | built |
| hidden effects | effects inferred; `[[std::pure]]` and `[[std::effects(...)]]` are checked ceilings | #207 | built |
| recursion | a loop; `list(T)` as an explicit stack | #207, #446 | built (direct calls); planned (through pointers, proved stack depth) |
| indexing or arithmetic on a raw pointer, `p < q` | a slice | #402 | planned |
| an array parameter, a run-time-sized stack array, `alloca` | a slice; `arena::push_slice` | #422 | planned |
| `setjmp`, `longjmp` | a `result`, or a task failure | #422, #324 | planned |
| a null pointer | `T *` is never null; `Option(T *)` may be | #86 | planned |
| `void *` converting silently | a written cast, then a typed API | #398, #402 | planned |
| a cast between pointer types, or pointer and integer | `mem::view(bytes, T)`, `mem::bytes(&x)` | #399, #402 | planned |
| casting away `const`, function-pointer casts | errors (#414's baseline) | #414 | planned |
| `memcpy`, `memset`, `memcmp` | `mem::copy`, `zero`, `fill`, `equal` | #399 | planned |
| `malloc`, `free`, `realloc` | the current allocator's region; `owned(T)` | #439, #399 | planned |
| a dangling pointer, use after free, a leak | regions and linear types, checked at compile time | #438, #439 | planned |
| a buffer grown by hand | `list(T)` | #449 | planned |
| an uninitialized variable | everything starts at zero; no constructor functions | #415 | planned |
| a C string, `printf`, varargs | `Str` is a slice; `format::` | #416, #199 | planned |
| a tag and a union kept by hand | a payload enum, narrowed by the compiler | #198, #402 | planned |
| a bare union | to be decided after #402's count | #402 | open |
| a sentinel, `errno`, an out parameter | `option`, `result` | #402 | planned |
| a callback with a `void *` context | a lambda, or a `closure(F)` to store it | #316, #400, #402 | planned |
| signed overflow, a bad shift, division by zero, float to integer out of range | checks in every build; `wrapping::` and `checked::` opt out | #351 | planned |
| a lossy implicit conversion | an error; a written narrowing cast is checked | #414, #351 | planned |
| a warning | an error, from a fixed baseline with a written "I meant it" form each | #414 | planned |
| an unused value | `_ = x;`, `_name` | #413 | planned |
| `typedef` | a tag is a type name; `constexpr Type` aliases | #333 | planned |
| `[]` with an initializer | `[_]` | #423 | planned |
| threads and shared mutable data | task groups and the five kinds of value | #436, #241 | planned |
| a mutable global | an error | #436 | planned |
| a lock taken by hand | `sync::with`, several locks in one call | #436 | planned |
| a channel | a generator, `stream::Stream(T)` | #436 | planned |
| a signal handler | `signal::stream(SIGINT, ...)` | #447 | planned |
| a stack overflow | the compiler proves each task's stack depth | #446 | planned |
| the world threaded from `main` | direct-call modules: `files`, `process`, `net`, `clock`, `console`, `env` | #240 | done |

## Every check speaks the same way (proposed)

- The first clause names the thing and the hazard: "`a` is a pointer, not an array".
- After the semicolon, the replacement as code: "take `I32 a[:]` (a slice)".
- A fix-it wherever the rewrite is mechanical, applied by `smith lint --fix` and the editor.
