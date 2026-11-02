---
type: architecture
title: std::stream, the runtime core of a lazy generator
description: A generator body runs as a fiber on its own stack that no scheduler owns and only next resumes, behind six C symbols the emitter calls; one pointer is a stream and null is an ended stream (issues #1543, #436, #1036).
tags: [stream, generator, fiber]
---
# std::stream

`lib/std/stream.smith` is the runtime under a generator: a function that returns `stream::Stream(T)` and hands out values with `yield` (#436). The new front end (#1428) lowers `yield` and `stream::next` to the calls below (#1546). This page is the runtime and its interface; the language part comes with the emitter. The library is written in today's syntax and has C names; the generic `Stream(T)` comes when the front end compiles generic libraries.

- **A stream is one pointer** to its record, and **null means ended**, so the zero value of a stream is an ended stream. `next` and `close` take the address of the pointer and set it to null when the stream ends, so nothing dangles after the end (a copy made before then can still go stale; #438's linear check will prevent that copy).
- **The body is a fiber**, the one of [std::task](task.md) (a guard page and 256 KB, the same switch), made with `fiber_start` and resumed only by `next` and `close`. It is lazy: `start` copies the arguments and maps the stack but runs nothing. Its locals are ordinary locals, so the emitter needs no state machine. The stream's handlers are a copy of its creator's at `start`, as for `spawn` (decided by the recommendation of #1543); `errno` and the handler table are swapped at each switch by std::task's `transfer`.
- **The interface** (names `smith_stream_<name>`):
  - `start(body, args, size)`: `body` is `void (*)(Stream *s, void *args)`; `args` is copied into memory the stream owns and handed to the body.
  - `next(&s, slot)`: false when `*s` is null. Otherwise it runs the body to its next `yield` (true, the value is in `slot`) or to its end (false: the stack and record are released and `*s` is null).
  - `slot(s)`: where the body writes the value it yields, which is the reader's `slot`.
  - `yield(s)`: called by the body after it writes the slot; returns true when `next` resumed it, false when the stream was closed. After false the body runs to its end, running its `defer`s, and calls `finish`. A `yield` after a close returns false at once.
  - `finish(s)` (noreturn): the body's end and every `return`. It marks the stream ended and switches back for good; the reader releases the stack, since a fiber cannot unmap the one it runs on. The frames it leaves are never unwound, so `fiber_release` unpoisons the whole stack for AddressSanitizer.
  - `close(&s)`: a stream that never ran is released without running its body; a suspended one is resumed with `yield` returning false, runs to `finish` and is released; null does nothing.
- **Cost.** One `next` and `yield` round trip, release profile (`-O2 -flto=thin`, checks on), arm64 Mac: 57 ns (10,000,000 iterations, median of 5). #436 expected 10 to 13 ns for the switch itself; the rest is the handler table and `errno` swaps and the contract checks at each switch.
- **Not here:** the spawned form and the bounded buffer (#1037), `stream::merge` (#1039), the rules about what may be yielded or moved (#436 part B), a generator that borrows its arguments (regions, #439), and the linear check that a stream is finished or closed (#438). A stack per generator is two mappings, so about 32,000 are alive at once under Linux's default `vm.max_map_count`.

Tests: `lib/std/stream_test.smith`.

## Generators in smith's own syntax

The new front end (#1428) emits a generator with `front/emit_stream.smith` (#1546); the checker's rules for `yield` are `front/types_stream.smith` (#1545).

```smith
import std::stream;

fn countdown(from: i32) -> stream::Stream(i32) {
    mut n := from
    while n > 0 {
        yield n
        n -= 1
    }
}
```

- **`stream::Stream(T)` is one `ptr`**, null once the stream has ended. A function that returns it and has a `yield` in its body is a generator; one without a `yield` is an ordinary function.
- **A generator is two functions.** The ramp keeps the function's own symbol and signature: it stores the arguments in a stack record, calls `smith_stream_start(body, &record, size)` and returns the handle, so nothing of the body runs (lazy). The body is the internal `<symbol>::body`, `void (ptr s, ptr args)`: it loads the parameters out of `args` into ordinary locals and runs the written body on the stream's own stack.
- **`yield x`** converts `x` to `T`, stores it where `smith_stream_slot(s)` points and calls `smith_stream_yield(s)`: true goes on, false (the stream was closed) runs the pending `defer`s and ends the body. **`return`** and the end of the body do the same: the `defer`s, then `smith_stream_finish(s)`. `return v` is an error: `yield v` first.
- **`stream::next(&s)`** is `?T`: `.some(v)` while the body yields, `.none` once it has ended (and after that, always). **`stream::close(&s)`** ends the stream early: an unread one never runs its body, a suspended one runs its `defer`s. `close` is the name the tickets propose; the lead has not confirmed it.
- **Parameters are copied by the ramp and nothing more.** A parameter that points into the caller's stack (a `*mut i32`, a slice) and is read after a `yield` is valid only while the caller's frame is: how it meets regions is #439, still open.
- **The checker does not type `stream::next` and `stream::close` yet**, so a module that calls them has no `Options.checked`: the emitter types a stream from the local that holds it. `for x in s` over a stream is not emitted (wave 3 of #1428, on top of `next`).

Tests: `tests/front_emit_stream_test.smith` and the golden IR `tests/front/emit/stream.ll`.
