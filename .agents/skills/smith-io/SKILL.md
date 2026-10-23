---
name: smith-io
description: "Using smith::io (import smith::io; io::Io, io::make, i32 main(io::Io *io)), the one value a program makes in main to touch the world: its arena, key, console, files, clock, processes, net and args. Load when code imports smith::io or includes <smith/io.h>, or on a question about doing io in .smith or C without globals."
---

# One io in main: smith's io

Everything a program does to the world outside it is a value you hold and pass on. It runs on macOS, on kqueue; operations complete in `io::Op`s the caller owns.

**io is the module `smith::io`** (`lib/smith/io.smith`): `import smith::io;`, then `io::Io`, `io::make`, `io::console_printf`, as the arena is `arena::Arena` (`import smith::arena;`) and the key `random::Key`. Its C API is the generated header `.smith/include/smith/io.h` (read it for the exact declarations); C includes it and uses the C names, which are the module's with a prefix, under "From C". The prose below names everything by its module name. The io library compiles with a smith that has clang inside, as the other libraries do.

## One io in main

```smith
import smith::format;
import smith::io;

i32 main(io::Io *io) {
    (void)io::console_printf(&io->console, "seed %llu\n", io->seed); // or, with no format string:
    (void)format::print(&io->console, "seed ", io->seed, "\n");     // typed pieces (smith-format)
    return 0; // the arguments are io->args.count and io->args.values
}
```

In a `.smith` file `main` may take the io: smith makes it, calls your `main` with a pointer to it, closes it after, and the status you return is the program's (an early `return` is fine). The lowering writes `static int32_t smith_user_main(SmithIo *io) { ... }` for your function and, at the end of the file, a C `main(int argc, char **argv)` that runs `smith_io_make(argc, argv)`, `smith_user_main(&io)`, `smith_io_close(&io)` and returns the status. The debugger shows that `main` at the line of yours, and your body, with a `module` line or none, also as `main` (its linkage name is `smith_user_main`). The form is exactly `i32 main(io::Io *name)` with `import smith::io;` (under whatever name the file binds it); `io::make` anywhere in a file with this `main` is an error ("main already has the io"), since the io is made already. A program has one `main`.

The C form stays, and it is how a renamed `.c` file works unchanged; it makes and closes the io itself, and so does `main` in a `.c` file (the io form is for `.smith`):

```smith
import smith::io;

i32 main(i32 argc, char **argv) {
    io::Io io = io::make(argc, argv);
    (void)io::console_printf(&io.console, "seed %llu\n", io.seed);
    io::close(&io);
    return 0;
}
```

The rest of this skill writes `io.` for a value and `io->` where the io is a pointer (the parameter of the io form).

`io` holds every capability as a field:

| Field | What it is |
| --- | --- |
| `io.arena` | a `arena::Arena` over a 64 GiB `mmap` reservation: address space, so only touched pages cost memory (AddressSanitizer poisons lazily, so its shadow grows with use too) |
| `io.key`, `io.seed` | the `random::Key` from `getentropy`, or from `SMITH_SEED` (decimal or `0x`) to replay a run; print `io.seed` when a run fails |
| `io.console` | stdin, stdout, stderr |
| `io.files` | the file system: any path the program is given, absolute or relative |
| `io.clock` | monotonic and wall time |
| `io.processes` | spawn, wait, run and capture |
| `io.net` | TLS connections (Network.framework) |
| `io.args` | `count`, `values` and the environment (`io::args_env`) |

There are no options.

Also in this family: [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).

A proved function may call io: every export of `smith::io` carries `[[clang::annotate("smith::edge")]]`, so `smith prove` treats a call of one as the edge of the program (smith-prove-trusted). `main` with a `contract::assert` is proved through io calls with no `contract::trusted()` of your own.
