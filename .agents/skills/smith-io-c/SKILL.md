---
name: smith-io-c
description: "Using smith::io from C (#include <smith/io.h>): the C names (SmithIo, smith_io_make, smith_files_read) that follow from the module's, a C capability in a header, and compiling the library in build.c with smith_library(b, "io"). Load when C code includes <smith/io.h> or a build.c compiles io."
---

# smith::io from C

Everything a program does to the world outside it is a value you hold and pass on. It runs on macOS (kqueue) and Linux (epoll); operations complete in `io::Op`s the caller owns.

## Module names and C names

A module's name is its C name without the prefix, and the C name is the one the header declares (the declarations are the same for both):

| In `.smith` | In C |
| --- | --- |
| a type: `io::Io`, `io::Console`, `io::Files`, `io::Op`, `io::Error`, `io::Read`, `io::Process`, `io::FileInfo`... | `Smith` and the name: `SmithIo`, `SmithConsole`, `SmithFiles`, `SmithOp`, `SmithError`, `SmithRead`... |
| a function of a capability: `io::files_read`, `io::console_printf`, `io::clock_now`, `io::processes_run`, `io::child_kill`, `io::net_connect`, `io::args_env`, `io::error_make` | `smith_` and the name: `smith_files_read`, `smith_console_printf`, `smith_clock_now`... |
| the io itself: `io::make`, `io::close`, `io::run`, `io::forever` | `smith_io_make`, `smith_io_close`, `smith_io_run`, `smith_io_forever` |
| the fake io: `io::Test`, `io::make_test`, `io::test_add_file`, `io::test_output`... | `SmithIoTest`, `smith_io_make_test`, `smith_io_test_add_file`, `smith_io_test_output`... |
| an enum's constant: `io::Replace`, `io::Atomic`, `io::Never`, `io::InputClosed` | the type's C name and the constant: `SmithWrite_Replace`, `SmithChildExit_Never`... |

**Bytes are slices.** `io::files_write`, `io::net_send` and `io::child_write_async` take a `const u8[:]` (C: `struct smith_array_const_u8_n`, `{.data = (const uint8_t *)p, .count = {n}}`), and the reads that fill a buffer (`io::net_receive`, `io::child_read_async`) a `u8[:]` (`struct smith_array_u8_n`); the count is the size or capacity. What a read returns is a `const u8[:]` too: `SmithRead.data` and `SmithOp.data` (`.data.data`, `.data.count[0]`; a file read or a line has a zero after it, not counted), and `SmithOp.taken` is the bytes a write took.

Nested modules do not exist, so a capability's functions are `io::<capability>_<name>` (`io::files_read`) where a future `io::files::read` might be; the capabilities and what they do would stay.

## From C

C includes the generated header and uses the C names, which are the module's names with a prefix (the table above); nothing in the declarations differs:

```c
#include <smith/io.h>

typedef struct { SmithFiles *files; SmithConsole *console; SmithArena *arena; } Editor;

Mesh mesh_load(SmithFiles *files, SmithArena *arena, const char *path);

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    Mesh mesh = mesh_load(&io.files, &io.arena, "cube.obj");
    Editor editor = {&io.files, &io.console, &io.arena};
    SmithRead text = smith_files_read(&io.files, &io.arena, "config.txt");
    if (text.error.code != 0) return 1;
    ...
}
```

A project's own capability in a C header: `typedef struct [[clang::annotate("smith::capability")]] { void *handle; } Window;`, its raw calls in `window.c` beside `window.h`, and `[[nodiscard, clang::annotate("smith::main_only")]] Window window_make(...)` for a constructor that only `main` may call. The fake io in C: `alignas(smith_arena_align_max) static unsigned char memory[1 << 20]; SmithIo io = smith_io_make_test((SmithIoTest){.memory = memory, .capacity = sizeof memory, .seed = 7});`.

## Get the library in build.c

io uses the arena and random, so compile all three, each with the project's own flags and `-I .smith/include` (see smith-build-objects for the `smith_object` form). io is written in `.smith`, so compiling it needs a smith with clang inside, as the arena and random do; C code that only includes `<smith/io.h>` compiles anywhere. A `.smith` project that imports it needs no build.c line: naming `import smith::io;` is enough to link it and the arena and random it uses.

```c
SmithObject io = {.compiler = "smith-cc", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
```

Add `io`, `random` and `arena` to the program's `.objects`.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md).
