---
name: smith-io-async
description: "Asynchronous calls in smith::io: io::Op values the caller owns, io::run and io::forever, kqueue underneath, timers from io::clock_*. Load when issuing overlapped reads, timers or connections, or on io::Op, io::run or io::forever."
---

# Asynchronous calls

To wait on several things in one loop, submit operations in `io::Op`s you own and run the loop:

```smith
import smith::io;

constexpr usize round_max = 1000;

void wait_for(io::Io *io, io::Process child) {
    io::Op timer = {}, exited = {};
    io::clock_timeout_async(&io->clock, &timer, 2'000'000'000);
    io::processes_wait_async(&io->processes, &exited, child);
    for (usize round = 0; round < round_max && !(timer.done && exited.done); round++)
        (void)io::run(io, io::forever); // or a timeout in ns; 0 only polls
}
```

`io::files_read_async`, `io::files_write_async`, `io::console_read_async`, `io::clock_timeout_async`, `io::processes_wait_async`, `io::child_write_async` and `io::child_read_async` submit; `io::run` completes what is ready and sets `op.done`, with `op.error`, `op.data`/`op.size`/`op.end` (reads) or `op.exit_status`/`op.signal` (waits). **Completion is a done flag, not a callback**: the result sits in the op, a loop in your own control flow reads it where your state is, no closure is needed. An op must not move or be freed while pending, and may be reused after done; paths and data it points at stay yours until done.

macOS has no asynchronous file IO, so file operations run synchronously, in submission order, inside `io::run`. Pipes, timers and process exits wait on kqueue; network connections are Network.framework's, whose callbacks run on a dispatch queue and push the finished op onto a lock-free list that wakes the same kqueue (`EVFILT_USER`), so they complete in `io::run` like the rest. Every blocking call is an op run to completion on this same loop.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
