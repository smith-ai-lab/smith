---
name: smith-io-errors
description: "Errors in smith::io: errors are values (io::Error, .error.code), and the blocking calls (io::files_read, io::console_print, io::console_printf, reading lines to the end of input). Load when handling a failed io call or reading a file or lines at once."
---

# Errors are values, and blocking calls

Every operation reports success or an `io::Error`: `code` is 0 or an errno value, `message` says what failed (`open config.txt: No such file or directory`). Nothing is printed behind your back. Only what the program cannot continue without stops it: failing to reserve the arena, a bad `SMITH_SEED`, and the arena's capacity stop.

```smith
import smith::io;

i32 main(i32 argc, char **argv) {
    io::Io io = io::make(argc, argv);
    io::Read text = io::files_read(&io.files, &io.arena, "config.txt"); // data is NUL-terminated
    if (text.error.code != 0) {
        io::Error printed = io::console_print_error(&io.console, text.error.message);
        (void)printed;
        return 1;
    }
    return 0;
}
```

## Blocking calls

For simple code: `io::files_read(files, arena, path)`, `io::files_write(files, path, data, size, io::Replace)`, `io::console_print(console, text)`, `io::console_print_error`, `io::console_read_line(console, arena)`, `io::clock_now(clock)` (monotonic ns), `io::clock_wall(clock)`, `io::clock_sleep(clock, ns)`, `io::processes_run(processes, arena, argv)` (stdout, stderr, exit status and signal in an `io::ProcessResult`; a missing program is `ENOENT`).

`io::console_printf(console, format, ...)` and `io::console_eprintf` format into a 4 KiB buffer and print (`EMSGSIZE` past it). They are not `[[nodiscard]]`: a report of a hundred lines has nowhere to say that stdout closed.

In `.smith` print typed pieces with no format string: `format::print(&io->console, "n = ", n, "\n")` and `format::print_error` (`import smith::format;`, skill smith-format) do the same through `io::console_print`; widths and `%g` are `format::pad`, `format::left`, `format::general`. The generic is in `format`, not `io`, because `io` is linked into smith, which its previous release builds, and a generic in it is the lag rule of smith-dev-build (a feature is usable there once it is on main).

Reading lines to the end of input is a loop with a bound and an end check, which the fake console makes testable:

```smith
import smith::io;

constexpr usize turn_max = 1000;

void play(io::Io *io) {
    for (usize turn = 0; turn < turn_max; turn++) {
        io::Read line = io::console_read_line(&io->console, &io->arena);
        if (line.error.code != 0 || line.end) break; // line.end: the input is over
        (void)io::console_printf(&io->console, "%s\n", line.data);
    }
}
```

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
