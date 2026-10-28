---
type: architecture
title: std::process, the direct-call child process module
description: Child processes as direct calls returning result(T, process::Error) (run, spawn, start with pipes, wait, kill), over a process handler of the thread, with a scripted fake and memory from the current allocator; the third replacement of io::Io's parts (issue #240).
tags: [process, handlers, result, mem]
---
# std::process

`lib/std/process.smith` (skill smith-process) is `io::Io`'s processes part without the `Io` value (issue #240, phase 1). It is additive: `io` is unchanged until the migration phases. Sockets to a child belong here (a child's stdio is a pair of pipes, `start`), and `net` keeps TCP and TLS only.

- **A handler of its own.** `SmithProcessHandler` (`include/smith/handlers.h`) has `start`, `read`, `write`, `close_input`, `wait`, `kill`, `close` and `cores`. No libc function is interposed (posix_spawn and waitpid are not reachable that way and pipes are plain descriptors the files handler would take): the module calls the members itself, so a thread with a handler never starts a real process, and none set is posix_spawn, pipes and waitpid. A child is an id the handler chooses; the module's `Child` keeps the descriptors of a real one. The setter returns the previous handler, as the others do.
- **Modes.** `start` makes a pipe to stdin and from stdout and leads a process group (`kill` reaches its children); `spawn` has no pipes; `run` captures stdout and stderr apart, reading both with `poll` so neither pipe fills while the other is read, then waits.
- **Errors.** What the world answers (a program not found, not allowed, no more processes, a write to a child that has gone, a signal to one that has) is a `process::Error` value. A bug is a checked failure (the panic-line audit of #240): a child closed twice or used after its close (EBADF), waited for twice, a write after `close_input`, a read from a stream the child lacks, and a fake that would block for ever. SIGPIPE never kills the program: a write to a closed pipe is `Broken`.
- **Memory.** The captured output and the fake's recorded stdin come from `mem::alloc` and belong to the caller's current region; nothing is freed one object at a time (#439). A test installs an arena and checks the heap saw nothing.
- **The fake** (`process::fake()`, scripts with `fake_add`) replaces io's fake programs and scripted children: the first script of a program that no child has used is the one a start uses; `exit_on` says when it has exited (`Start`, `InputClosed`, `OutputRead`, `Never`); a read or wait that nothing could ever finish is a checked failure, since a synchronous test cannot advance time.
- **Names.** The C functions are `smith_process_*` beside io's `smith_processes_*` and `smith_child_*`; the types are `SmithProcess*`.
- **Not here.** Async waits, reads and writes (`io::Op`, `io::run`) wait for the task runtime (#241); the captured stdout and stderr in one interleaved stream (io's `Capture`) and a terminal child (`spawn_terminal`) are not ported yet.
