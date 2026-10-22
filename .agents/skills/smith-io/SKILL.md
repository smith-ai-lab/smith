---
name: smith-io
description: "Using smith::io (import smith::io; <smith/io.h> from C), smith's io: one io::Io (SmithIo) made in main holds the program's arena, key, console, files, clock, processes and args, and a fake io for tests. Load when code imports smith::io or includes <smith/io.h>, when a build.c calls smith_library(b, \"io\"), or on a question about files, console, clock or processes in .smith or C without globals, async io, or testing code that does io."
---

# One io in main: smith's io

This skill ships in the smith plugin, from `.agents/skills/smith-io/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Everything a program does to the world outside it is a value you hold and pass on. It runs on macOS, on kqueue; operations complete in `io::Op`s the caller owns.

**io is the module `smith::io`** (`lib/smith/io.smith`): `import smith::io;`, then `io::Io`, `io::make`, `io::console_printf`, as the arena is `arena::Arena` (`import smith::arena;`) and the key `random::Key`. Its C API is the generated header `.smith/include/smith/io.h` (read it for the exact declarations); C includes it and uses the C names, which are the module's with a prefix, under "From C". The prose below names everything by its module name. The io library compiles with a smith that has clang inside, as the other libraries do.

## Module names and C names

A module's name is its C name without the prefix, and the C name is the one the header declares (the declarations are the same for both):

| In `.smith` | In C |
| --- | --- |
| a type: `io::Io`, `io::Console`, `io::Files`, `io::Op`, `io::Error`, `io::Read`, `io::Process`, `io::FileInfo`... | `Smith` and the name: `SmithIo`, `SmithConsole`, `SmithFiles`, `SmithOp`, `SmithError`, `SmithRead`... |
| a function of a capability: `io::files_read`, `io::console_printf`, `io::clock_now`, `io::processes_run`, `io::child_kill`, `io::net_connect`, `io::args_env`, `io::error_make` | `smith_` and the name: `smith_files_read`, `smith_console_printf`, `smith_clock_now`... |
| the io itself: `io::make`, `io::close`, `io::run`, `io::forever` | `smith_io_make`, `smith_io_close`, `smith_io_run`, `smith_io_forever` |
| the fake io: `io::Test`, `io::make_test`, `io::test_add_file`, `io::test_output`... | `SmithIoTest`, `smith_io_make_test`, `smith_io_test_add_file`, `smith_io_test_output`... |
| an enum's constant: `io::Replace`, `io::Atomic`, `io::Never`, `io::InputClosed` | the type's C name and the constant: `SmithWrite_Replace`, `SmithChildExit_Never`... |

Nested modules do not exist, so a capability's functions are `io::<capability>_<name>` (`io::files_read`) where a future `io::files::read` might be; the capabilities and what they do would stay.

## One io in main

```smith
import smith::io;

int main(int argc, char **argv) {
    io::Io io = io::make(argc, argv);
    (void)io::console_printf(&io.console, "seed %llu\n", io.seed);
    return 0;
}
```

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

## Pass only what is needed

main hands each function the narrowest thing that covers what it uses, so a function that takes no clock cannot depend on time and one that takes no files cannot touch the disk.

```smith
import smith::arena;
import smith::io;

typedef struct {
    usize vertices;
} Mesh;

// One or two capabilities: those alone.
Mesh mesh_load(io::Files *files, arena::Arena *a, const char *path) {
    io::Read text = io::files_read(files, a, path);
    return (Mesh){.vertices = text.error.code == 0 ? text.size : 0};
}

// Most of io: the whole of it.
void game_run(io::Io *io) {
    (void)io::console_printf(&io->console, "running\n");
}

// A bundle several functions share: a struct the project defines, with what that group needs.
typedef struct {
    io::Files *files;
    io::Console *console;
    arena::Arena *arena;
} Editor;

void editor_open(Editor *editor, const char *path) {
    Mesh mesh = mesh_load(editor->files, editor->arena, path);
    (void)io::console_printf(editor->console, "%zu\n", mesh.vertices);
}

int main(int argc, char **argv) {
    io::Io io = io::make(argc, argv);
    Mesh mesh = mesh_load(&io.files, &io.arena, "cube.obj");
    Editor editor = {&io.files, &io.console, &io.arena};
    editor_open(&editor, "cube.obj");
    game_run(&io);
    return mesh.vertices == 0 ? 0 : 1;
}
```

- One or two capabilities: those alone (`mesh_load`).
- Most of io: the whole of it (`game_run`).
- A bundle several functions share: a struct the project defines (`Editor`).

If a signature wants four or more separate capabilities, pass `io::Io *` or define the struct. A random key is passed alone (`io.key`; derive one per use, see smith-random).

## smith-effects: the model, checked

`smith lint` (and the editor) run the clang-tidy check `smith-effects`, which makes the model above a rule: a program touches the world only through capabilities passed down from `main`, so a function's signature says what it can do. It is on in smith's default `.clang-tidy` (a project that does not want it runs `smith lint --dump-config > .clang-tidy` and deletes `smith-effects` from `Checks`). Everything is decided per function from the source; no file is exempt.

The rules:
1. **A raw effect is a call into a system header that is not on the pure allow list** (`<string.h>`, `<math.h>`, `<ctype.h>`, `<stdint.h>`, `<stddef.h>`, `<float.h>`, `<limits.h>`, `<stdbit.h>`, `<stdckdint.h>`, `snprintf`, `vsnprintf`, `sscanf`, the `strto*` family, `qsort`, `bsearch`, `abs`, `div`, `__builtin_*`), or an Objective-C message to a system framework's class or object (`[NSFileManager defaultManager]`). The list is `shim/effects.h` in smith; a call the list does not name is reported. In a `.smith` file the finding names the callee as written (`c::fopen`).
2. **Failure is not an effect:** `abort`, `assert`, `__builtin_trap`, `__builtin_unreachable`, `smith_failure`, the contract calls (`contract::assert` included) and the checks of `smith::test` are allowed everywhere.
3. **Raw effects are allowed in a capability's implementation.** A capability is a type marked `[[clang::annotate("smith::capability")]]`: `io::Io`, `io::Console`, `io::Files`, `io::Clock`, `io::Processes`, `io::Net` and `io::Args` (the process's inputs: the live environment), and the handles `io::Op`, `io::Capture`, `io::Child` and `io::Connection`. (`random::Key` and `arena::Arena` are values, not capabilities.) Its implementation is the module that defines it: the `.smith` module file, or for C the `.c` file beside the header that defines it (`io.c` with `io.h`; `lib/io.c` or `src/io.c` with a header under the `include/` beside them); `smith::io` is itself a `.smith` module, so it is its own implementation. Every function there, private helpers included, may make raw calls.
4. **An exported function of that module that reaches a raw effect, directly or through the module's own static functions, must take or return a capability** (or a struct holding one). Otherwise make it `static`.
5. **A main-only constructor may be called only inside `main`:** `io::make`, marked `[[clang::annotate("smith::main_only")]]`. `io::make_test` is not: tests make fakes anywhere.
6. **No capability in static storage:** a global or `static` local of a capability type, a pointer to one, or a struct that holds one.

One option, `smith-effects.CFiles` (default true): `false` makes the check skip `.c` and `.m` files and read `.smith` only (smith's own `.clang-tidy` does this while its C is ported).

**Marking a project's own capability** (a window, a GPU device), in a `.smith` module (a C header is under "From C"):

```smith
module window;

export typedef struct [[clang::annotate("smith::capability")]] {
    void *handle;
} Window;

// Raw calls into the platform go here, in the module, and every exported function takes a Window *.
export bool is_open(const Window *window) {
    return window->handle != nullptr;
}

// A constructor that may be called only in main.
export [[nodiscard, clang::annotate("smith::main_only")]] Window make(void) {
    return (Window){.handle = nullptr};
}
```

The rest of the program takes a `window::Window *` like any other capability.

**Fixing a finding:**

| Finding | Fix |
| --- | --- |
| `fopen`, `open`, `stat`, `rename`... touch the world | take an `io::Files *` and call `io::files_read`, `io::files_write`... |
| `printf`, `puts`, `fputs(stderr)`, `getchar` | take an `io::Console *`: `io::console_printf`, `io::console_print`, `io::console_read_line` |
| `time`, `clock_gettime`, `sleep` | take an `io::Clock *`: `io::clock_now`, `io::clock_wall`, `io::clock_sleep` |
| `fork`, `system`, `popen`, `waitpid` | take an `io::Processes *`: `io::processes_run`, `io::processes_spawn` |
| `socket`, `connect`, `send` | take an `io::Net *`: `io::net_connect`, `io::net_send`, `io::net_receive` |
| `malloc`, `free` | take a `SmithArena *`: `smith_arena_push`, or `arena::push` in `.smith` |
| `rand`, `arc4random` | take a `SmithKey` and derive from it (smith-random) |
| `getenv` | `io::args_env(&io.args, "NAME")` |
| `exit` | return the status from `main` |
| an exported function "reaches `X` but takes no capability" | add the capability parameter it needs, or make it `static` |
| `io::make` "may be called only in main" | take the `io::Io *` (or the part) as a parameter; a test uses `io::make_test` |
| a capability "cannot be kept in static storage" | pass it as a parameter from `main` |
| an Objective-C message to a framework | wrap the framework in a capability's implementation and take that capability |

## Errors are values

Every operation reports success or an `io::Error`: `code` is 0 or an errno value, `message` says what failed (`open config.txt: No such file or directory`). Nothing is printed behind your back. Only what the program cannot continue without stops it: failing to reserve the arena, a bad `SMITH_SEED`, and the arena's capacity stop.

```smith
import smith::io;

int main(int argc, char **argv) {
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

## Write modes, folders, maps, the environment

These block and run at once (they do not queue behind pending asynchronous file operations), on the real io and the fake alike:

| Call | Does |
| --- | --- |
| `io::files_info(files, path)` | `folder`, `writable`, `size`; `ENOENT` if nothing is there |
| `io::files_list(files, arena, folder)` | entries (`name`, `folder`, `size`) sorted by name, without `.` and `..` |
| `io::files_make_folder(files, path)` | like `mkdir -p`; a folder that is there is success |
| `io::files_write(files, path, data, size, mode)` | writes the bytes as the mode says: `io::Replace` (create or replace), `io::Atomic` (write `<path>.tmp` and rename it over the path, so nobody sees half a file), `io::Append` (add to the end, creating the file) or `io::New` (create only if there is none: `EEXIST` otherwise, which is how a program makes a unique name) |
| `io::files_remove(files, path)` | deletes a file (a folder is `EPERM`) |
| `io::files_map(files, path)` | maps the file: nothing is read until the memory is touched, so a 150 MB asset costs only the pages used. Private and writable (writes never reach the file), for the life of the program; an empty file is `EINVAL`. The fake gives a copy |
| `io::args_env(&io.args, "NAME")` | the variable's value, or `nullptr`; the real io asks the process each time |
| `io::processes_cores(&io.processes)` | how many processes can run at once |

For several arenas with different lifetimes, such as one reset every frame, carve them out of `io.arena` with `smith_arena_carve` (see smith-arena): the 64 GiB reservation commits pages only as they are used.

Reading a large file with `io::files_read` copies it into the arena; mapping it does not. Choose the map for assets read in place, the read for text.

A program that runs several copies of itself spawns `args.values[0]` with `io::processes_spawn`, waits with `io::processes_wait_async` on all of them in one loop, and gets their results from files they wrote with `io::files_write` and `io::Atomic`, in a folder named from `io.key` under `$TMPDIR`; each copy builds what it needs again, since nothing is shared.

## Many processes, each with its output whole

`io::processes_run` blocks until one process ends. To run several at once and get each one's output when it ends (a build's jobs), start them with `io::processes_capture_async(&io.processes, &arena, argv, &capture)` and keep the `io::Capture` where it is. Its stdout and stderr share one pipe, so the lines are in the order the process wrote them, and the process leads a process group of its own (`kill(-capture.process.id, SIGINT)` reaches its children too). After each `io::run`, call `io::capture_poll(&capture)` for every capture still running: it takes in what the loop completed, submits the next read, and returns true once the process has exited and its output is read to the end. Then `capture.exit_status`, `capture.signal`, `capture.output` and `capture.output_size` are set. Give each capture an arena it can fill: the output grows in the arena by doubling, and when the arena is full the process still runs to its end, `capture.truncated` is set and the end of the output is dropped.

`io::processes_spawn_terminal` starts a program that talks to the person at the terminal: it keeps this process's stdin, stdout and stderr and stays in this process group, so a Ctrl-C reaches it. (`io::processes_spawn` gives the program `/dev/null` for stdin.) Wait for it with `io::processes_wait_async`.

## A child you talk to over pipes

For a program that drives another (a server holding debugger children that speak DAP), `io::processes_start_piped(&io.processes, argv, &child)` starts `argv[0]` (found on `PATH`; a missing program is `ENOENT`) with a pipe for its stdin and a pipe for its stdout, and returns at once. The child leads a process group of its own. Its stderr is the program's own, not a pipe: a child that logs there can never block on a full pipe nobody reads. The pipes carry raw bytes; framing (lines, `Content-Length:`) is the caller's. Keep the `io::Child` where it is until `io::child_close`.

- `io::child_write_async(&child, &op, data, size)` completes when all the bytes are in the pipe, or fails: `EPIPE` when the child closed its stdin or exited (`op.size` is what was taken first). SIGPIPE cannot kill the program. A write bigger than the pipe (64 KiB) completes as the child reads, so write and read together on one loop and nothing deadlocks. `data` stays yours until done.
- `io::child_read_async(&child, &op, buffer, capacity)` completes with whatever has arrived (`op.data`, `op.size`, at least one byte, not NUL-terminated), or `op.end` when the child closed its stdout.
- `io::child_close_input(&child)` closes its stdin, so it sees the end of input (`cat` exits). `io::child_kill(&child, signal)` signals the whole group (`ESRCH` if there is none). `io::child_wait_async(&child, &op)` waits for the exit; once that op is done the child's id is free for another program, so `io::child_kill` returns `ESRCH` and sends nothing (wait with `io::processes_wait_async(&io.processes, &op, child.process)` and the kill cannot know: then never kill an id after its wait). The same holds for `io::capture_stop(&capture, signal)`, which signals a capture's group only while its process has not been waited for. `io::child_close(&child)` closes the pipes still open, once, after the last op is done and the child has exited.
- One write, one read and one wait may be pending on a child at once; several children, the program's own stdin (`io::console_read_async`), timers and exits all wait in the same `io::run`.

```smith
import smith::io;

void drive(io::Io *io, const char *request, usize request_size) {
    const char *const argv[] = {"smith", "dap", nullptr};
    io::Child child = {};
    io::Error error = io::processes_start_piped(&io->processes, argv, &child);
    if (error.code != 0) return;
    io::Op sent = {}, received = {}, exited = {};
    char buffer[4096];
    io::child_write_async(&child, &sent, request, request_size);
    io::child_read_async(&child, &received, buffer, sizeof buffer);
    io::child_wait_async(&child, &exited);
    // loop on io::run; resubmit the read when received.done and !received.end; finish when exited.done
    io::child_close(&child);
}
```

In the fake, script the child first: `io::test_add_child(&io, (io::ChildScript){.program = "smith", .output = "Content-Length: 2\r\n\r\n{}", .chunk = 8, .exit_on = io::InputClosed, .status = 0})`. The first unused script of the program is the child started. It writes its output from the start (in pieces of `chunk` bytes if set); a read with nothing left waits, and ends once the child has exited. `exit_on` says when it exits: `io::Never` (only a kill ends it, with that signal), `_Start` (at once, like `true`: a write is `EPIPE`), `_InputClosed` (like `cat`) or `_OutputRead`. What the program wrote to its stdin is `io::test_child_input(&io, n)` for the n-th script added (8 KiB kept). Up to 16 children.

## Asynchronous calls

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

## The network

`io.net` makes TLS connections with the platform's stack (Network.framework, loaded with `dlopen` on the first connection, so a program that never connects pays nothing at start). An `io::Connection` is a struct you own and keep in place until its close is done. Everything is an op, with a blocking form:

```smith
import smith::io;

void fetch(io::Io *io, const char *request, usize request_size) {
    io::Connection connection = {};
    io::Error error = io::net_connect(&io->net, &connection, "example.com", 443, true); // tls
    error = io::net_send(&io->net, &connection, request, request_size);
    char buffer[4096];
    io::Read reply = io::net_receive(&io->net, &connection, buffer, sizeof buffer); // reply.size bytes, or reply.end
    (void)reply;
    error = io::net_close(&io->net, &connection);
    (void)error;
}
```

`io::net_connect_async`, `io::net_send_async`, `io::net_receive_async` and `io::net_close_async` take an `io::Op` like the other async calls. One send and one receive may be pending on a connection at once. A receive completes with whatever has arrived (at least one byte, at most `capacity`, not NUL-terminated), or `end` when the peer is done. Close completes what is still pending with `ECANCELED` and is the last thing to touch the connection, so free it only after the close is done; a connection that failed is closed like one that worked.

Failures are values: `ECONNREFUSED` and the other errnos of the system, `ENOENT` for a name that does not resolve, and `EAUTH` for a certificate or handshake failure with the reason in the message ("the certificate has expired"). The system's trust decides and there is no way to turn the check off.

## The test io

A test that takes `io::Io *io` (`test_x(io::Io *io)`, smith-test) gets a real io the runner made for it, seeded from the run's seed and with a scratch folder of its own (`test::scratch(io)`, also `$TMPDIR`), removed when the test passes. A test never makes a real io itself (`io::make` is for `main`, and smith-effects reports it anywhere else); to test code that takes a capability on a fake, make one:

```smith
import smith::arena;
import smith::io;

int main(void) {
    alignas(arena::align_max) static u8 memory[1 << 20];
    io::Io io = io::make_test((io::Test){
        .memory = memory, .capacity = sizeof memory, .seed = 7, .input = "A3 B4\nC5 D6\n"});
    (void)io::test_add_file(&io, "config.txt", "volume=3\n");
    (void)io::test_add_program(&io, "git", "main\n", 0);
    return 0;
}
```

It is the same `io::Io`, so code under test cannot tell it from the real one: files are in memory (writes land there too), the console reads the scripted `.input` then reports the end, output is captured (`io::test_output(&io)`, `io::test_errors(&io)`), the clock moves only when `io::clock_advance(&io.clock, ns)` says so (and `io::clock_sleep` of a non-zero time is `EDEADLK`, since nothing can wake it), the key comes from `.seed`, programs are scripted, and its memory is your buffer (a quarter holds file contents). `io::test_add_server(&io, (io::NetServer){.host, .port, .response, .response_size, ...})` scripts a server of the network: the first connection to `host:port` that no connection has used gets the response (`.chunk` bytes at a time if set), then a clean end or, with `.error`, that errno; `.tls_failure` makes the connect fail with `EAUTH` and that message; a host with no server is `ECONNREFUSED`; `io::test_request(&io, host, port, n)` is what the n-th connection sent. Adding a server to the real io scripts its network only (files, console and clock stay real), which is how code that downloads is tested on a real disk. `io::Test.environment` (`"NAME=value"` entries) and `.cores` give it an environment and a core count; folders exist as soon as a file is under them, or `io::files_make_folder` made them (a write into a folder that was never made still succeeds, where the real io says `ENOENT`). `io::run` on it never waits: it completes what is ready now. Errors have the same codes and messages as the real io.

Write the function once against capabilities, call it with `&io.files` from main and with the fake's in a test, and compare. The scripted console drives a line loop to completion including the end of input, which is the bug a program that spins on EOF has.

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

io uses the arena and random, so compile all three, each with the project's own flags and `-I .smith/include` (see smith-build for the `smith_object` form). io is written in `.smith`, so compiling it needs a smith with clang inside, as the arena and random do; C code that only includes `<smith/io.h>` compiles anywhere. A `.smith` project that imports it needs no build.c line: naming `import smith::io;` is enough to link it and the arena and random it uses.

```c
SmithObject io = {.compiler = "smith-cc", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
```

Add `io`, `random` and `arena` to the program's `.objects`.
