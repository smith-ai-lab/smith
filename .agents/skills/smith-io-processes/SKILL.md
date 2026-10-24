---
name: smith-io-processes
description: "Processes in smith::io: io::processes_run for many children with each output whole, and io::child_* for a child you talk to over pipes (io::Child, io::Capture, io::child_kill). Load when running a command, capturing its output or driving a child over stdin and stdout."
---

# Processes and children

## Many processes, each with its output whole

`io::processes_run` blocks until one process ends. To run several at once and get each one's output when it ends (a build's jobs), start them with `io::processes_capture_async(&io.processes, &arena, argv, &capture)` and keep the `io::Capture` where it is. Its stdout and stderr share one pipe, so the lines are in the order the process wrote them, and the process leads a process group of its own (`kill(-capture.process.id, SIGINT)` reaches its children too). After each `io::run`, call `io::capture_poll(&capture)` for every capture still running: it takes in what the loop completed, submits the next read, and returns true once the process has exited and its output is read to the end. Then `capture.exit_status`, `capture.signal`, `capture.output` and `capture.output_size` are set. Give each capture an arena it can fill: the output grows in the arena by doubling, and when the arena is full the process still runs to its end, `capture.truncated` is set and the end of the output is dropped.

`io::processes_spawn_terminal` starts a program that talks to the person at the terminal: it keeps this process's stdin, stdout and stderr and stays in this process group, so a Ctrl-C reaches it. (`io::processes_spawn` gives the program `/dev/null` for stdin.) Wait for it with `io::processes_wait_async`.

## A child you talk to over pipes

For a program that drives another (a server holding debugger children that speak DAP), `io::processes_start_piped(&io.processes, argv, &child)` starts `argv[0]` (found on `PATH`; a missing program is `ENOENT`) with a pipe for its stdin and a pipe for its stdout, and returns at once. The child leads a process group of its own. Its stderr is the program's own, not a pipe: a child that logs there can never block on a full pipe nobody reads. The pipes carry raw bytes; framing (lines, `Content-Length:`) is the caller's. Keep the `io::Child` where it is until `io::child_close`.

- `io::child_write_async(&child, &op, data)` (`data` is a `const u8[:]`) completes when all the bytes are in the pipe, or fails: `EPIPE` when the child closed its stdin or exited (`op.size` is what was taken first). SIGPIPE cannot kill the program. A write bigger than the pipe (64 KiB) completes as the child reads, so write and read together on one loop and nothing deadlocks. `data` stays yours until done.
- `io::child_read_async(&child, &op, buffer)` (`buffer` is a `u8[:]`, its count the capacity) completes with whatever has arrived (`op.data`, a `const u8[:]` of at least one byte, not NUL-terminated), or `op.end` when the child closed its stdout.
- `io::child_close_input(&child)` closes its stdin, so it sees the end of input (`cat` exits). `io::child_kill(&child, signal)` signals the whole group (`ESRCH` if there is none). `io::child_wait_async(&child, &op)` waits for the exit; once that op is done the child's id is free for another program, so `io::child_kill` returns `ESRCH` and sends nothing (wait with `io::processes_wait_async(&io.processes, &op, child.process)` and the kill cannot know: then never kill an id after its wait). The same holds for `io::capture_stop(&capture, signal)`, which signals a capture's group only while its process has not been waited for. `io::child_close(&child)` closes the pipes still open, once, after the last op is done and the child has exited.
- One write, one read and one wait may be pending on a child at once; several children, the program's own stdin (`io::console_read_async`), timers and exits all wait in the same `io::run`.

```smith
import smith::io;

void drive(io::Io *io, const u8 request[:]) {
    const char *const argv[] = {"smith", "dap", nullptr};
    io::Child child = {};
    io::Error error = io::processes_start_piped(&io->processes, argv, &child);
    if (error.code != 0) return;
    io::Op sent = {}, received = {}, exited = {};
    u8 buffer[4096];
    io::child_write_async(&child, &sent, request);
    io::child_read_async(&child, &received, buffer);
    io::child_wait_async(&child, &exited);
    // loop on io::run; resubmit the read when received.done and !received.end; finish when exited.done
    io::child_close(&child);
}
```

In the fake, script the child first: `io::test_add_child(&io, (io::ChildScript){.program = "smith", .output = "Content-Length: 2\r\n\r\n{}", .chunk = 8, .exit_on = io::InputClosed, .status = 0})`. The first unused script of the program is the child started. It writes its output from the start (in pieces of `chunk` bytes if set); a read with nothing left waits, and ends once the child has exited. `exit_on` says when it exits: `io::Never` (only a kill ends it, with that signal), `_Start` (at once, like `true`: a write is `EPIPE`), `_InputClosed` (like `cat`) or `_OutputRead`. What the program wrote to its stdin is `io::test_child_input(&io, n)` for the n-th script added (8 KiB kept). Up to 16 children.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
