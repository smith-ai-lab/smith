---
name: smith-process
description: "Using std::process (import std::process; process::run, spawn, start, write, read, close_input, wait, kill, close, process::fake): child processes by direct calls that return a result, with a scripted fake for tests. Load when code imports std::process or includes <smith/process.h>, or on running a program, capturing its output or talking to a child's stdin and stdout."
---

# Child processes by direct calls: std::process

Call `process::` anywhere. A call returns `Result(T, process::Error)`; the error is what the world said (`NotFound`, `Denied`, `TooMany`, `Broken` for a write to a child that has gone, `Gone` for a signal to one that has, `Other`). A program is a slice of C strings, `argv[0]` found on `PATH`. Memory a call returns comes from the current allocator (smith-mem) and is the caller's region.

```smith
module scratch;
import std::arena;
import std::mem;
import std::process;
export I32 status_of_echo(arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a);
    mem::Allocator *before = mem::set_allocator(&allocator.base);
    defer mem::set_allocator(before);
    const char *const argv[_] = {"echo", "hi"};
    process::OutputResult done = process::run(argv[0:2]);
    switch (done) {
    case .ok: return done.ok.exit.status;
    case .error: return -1;
    }
}
```
| Call | Does |
| --- | --- |
| `run(argv)` | starts with no stdin, captures stdout and stderr apart, waits: `Output{exit, output, errors}`; `Exit{status, signal}` (status 0 when a signal killed it) |
| `spawn(argv)` / `start(argv)` | a `Child` at once: with no pipes (stdin is /dev/null) / with a pipe to its stdin and one from its stdout, leading a process group |
| `write(&child, data)` / `read(&child, buffer)` | write all of the bytes (`Broken` when the child is gone); read at least one byte, 0 is the end. Both block |
| `close_input(&child)` | the child sees the end of its input (`cat` exits); a second call does nothing |
| `wait(&child)` / `kill(&child, signal)` | the `Exit`; the signal goes to the child's group, `Gone` once it has exited or been waited for |
| `close(&child)` / `cores()` | releases the pipes (no kill or wait) / how many processes can run at once |

- **Misuse is a checked failure**: closing a child twice, using it after `close`, `wait` twice, `write` after `close_input`, `read` from a child with no stdout pipe, a fake that would wait for ever.

**The fake, for tests** (a scripted program runs nothing):
```smith
module scratch_fake;
import std::arena;
import std::mem;
import std::process;
export Usize lines_built(arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a);
    mem::Allocator *outside = mem::set_allocator(&allocator.base);
    defer mem::set_allocator(outside);
    process::Fake fake = process::fake();
    process::fake_add(&fake, (process::Script){.program = "build", .output = "ok\n", .status = 0});
    process::Handler *before = process::set_handler(&fake.base);
    defer process::set_handler(before);
    const char *const argv[_] = {"build"};
    process::OutputResult done = process::run(argv[0:1]);
    switch (done) {
    case .ok: return countof(done.ok.output);
    case .error: return 0;
    }
}
```
A script says what a child writes (`output`, `errors`, in pieces of `chunk` bytes), when it exits (`exit_on`: `Start` at once, `InputClosed` like `cat`, `OutputRead`, `Never` until killed) and its `status`; `fake_input(&fake, n)` is what the program wrote to the n-th child's stdin; `fake_set_cores`. C names are `smith_process_*`. Sockets to a child are these pipes; async waits wait for #241. See also [smith-files](../smith-files/SKILL.md), [process](../../../knowledge/process.md).
