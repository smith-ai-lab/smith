---
type: architecture
title: console, env and clock are small modules called directly
description: std::console, std::env and std::clock are typed functions called with no value threaded from main, over the console, env and clock handlers of the runtime, so a test fakes them.
tags: [libraries, console, env, clock, handlers]
---
# console, env and clock

The world is called directly (#240) and a test stands in for it with a handler (#239). `console`, `env` and `clock` are three of the modules; `files`, `process` and `net` are the others.

- **Called directly, no capability.** `console::print("n = ", n, "\n")`, `env::get("HOME")`, `clock::sleep(250 * clock::millisecond)`. What a function does is its inferred effect (`console`, `env`, `clock`), checked by `[[std::pure]]` and `[[std::effects(...)]]` (knowledge/effect-inference.md).
- **Over the handlers.** `console` calls `write` and `read` on fds 0 to 2, which `SmithConsoleHandler` already answers; `env::get` calls `getenv`, which `SmithEnvHandler {get}` answers; `clock` calls `clock_gettime` and `nanosleep`, which `SmithClockHandler {now, sleep}` answers, with `usleep` and `sleep`. Each handler is per thread, set with a setter that returns the previous one (`console::set_handler`, `env::set_handler`, `clock::set_handler` wrap them), and none set is libc's (knowledge/handlers.md). The clock handler speaks nanoseconds (not `struct timespec`, which a `.smith` signature cannot name yet), so a fake is a few lines, and **a fake clock's `sleep` advances its time and returns**: it never blocks and never fails.
- **Typed.** Functions return their results; slices, not pointer and length; `env::get` returns an `option` (absent is `.none`); `console` writes and reads return a `result` with a small `console::Error` (`Closed`, `Full`, `Failed`) instead of an errno. **Real OS errors are values, misuse is a checked failure**: a `read` into no room, an empty or over-long variable name, a sleep past half of `U64`.
- **Formatted print has no limit.** `console::print(pieces...)` uses `format::to` into a 512-byte stack buffer and, for a longer text, into memory of the allocator in force that it frees.
- **C names.** The C names are `smith_console_print_pieces`, `smith_console_line`, `smith_clock_now_epoch` and `smith_clock_sleep_ns`.
- **Not here.** The program's arguments are `main(Str args[:])` (#240 phase 2); async reads and timers wait for the task runtime (#241); `gettimeofday`, `time` and the Mach clocks are not reached by the clock handler.

Tests: `lib/std/console_test.smith`, `env_test.smith`, `clock_test.smith` (fake handlers) and `tests/handlers/envclock.c` (the runtime). Skills: smith-console, smith-env, smith-clock, smith-entropy (`lib/std/entropy_test.smith`).

`entropy::seed()` (`std::entropy`, issues #240 and #500) is the same idea for randomness: a fresh `random::Key` from `getentropy`, which `SmithEntropyHandler {fill}` answers per thread, with the `entropy` effect inferred from the call. It lives in its own module so `std::random` stays pure (a key is a value made from a seed; the system's randomness is one effect in one place, and `random` imports no system header). `entropy::fake(number)` is the fake with fixed bytes; `entropy::set_handler` puts a handler in force.
