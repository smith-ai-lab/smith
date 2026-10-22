---
name: smith-io
description: "Using <smith/io.h>, smith's io: one SmithIo made in main holds the program's arena, key, console, files, clock, processes and args, and a fake io for tests. Load when code includes <smith/io.h>, when a build.c calls smith_library(b, \"io\"), or on a question about files, console, clock or processes in C without globals, async io, or testing code that does io."
---

# One io in main: smith's io

This skill ships in the smith plugin, from `.agents/skills/smith-io/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Everything a program does to the world outside it is a value you hold and pass on. The header is `.smith/include/smith/io.h`; read it first. It runs on macOS, on kqueue; operations complete in `SmithOp`s the caller owns.

## One io in main

```c
#include <smith/io.h>

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    ...
}
```

`io` holds every capability as a field:

| Field | What it is |
| --- | --- |
| `io.arena` | a `SmithArena` over a 64 GiB `mmap` reservation: address space, so only touched pages cost memory (AddressSanitizer poisons lazily, so its shadow grows with use tooitted) |
| `io.key`, `io.seed` | the key from `getentropy`, or from `SMITH_SEED` (decimal or `0x`) to replay a run; print `io.seed` when a run fails |
| `io.console` | stdin, stdout, stderr |
| `io.files` | the file system: any path the program is given, absolute or relative |
| `io.clock` | monotonic and wall time |
| `io.processes` | spawn, wait, run and capture |
| `io.net` | TLS connections (Network.framework) |
| `io.args` | `count`, `values` and the environment (`smith_args_env`) |

There are no options.

## Pass only what is needed

main hands each function the narrowest thing that covers what it uses, so a function that takes no clock cannot depend on time and one that takes no files cannot touch the disk.

- One or two capabilities: those alone.

  ```c
  Mesh mesh_load(SmithFiles *files, SmithArena *arena, const char *path);
  Mesh mesh = mesh_load(&io.files, &io.arena, "cube.obj");
  ```

- Most of io: the whole of it.

  ```c
  void game_run(SmithIo *io);
  ```

- A bundle several functions share: a struct the project defines, with what that group needs.

  ```c
  typedef struct { SmithFiles *files; SmithConsole *console; SmithArena *arena; } Editor;
  Editor editor = {&io.files, &io.console, &io.arena};
  editor_open(&editor, path);
  editor_save(&editor, path);
  ```

If a signature wants four or more separate capabilities, pass `SmithIo *` or define the struct. A random key is passed alone (`io.key`; derive one per use, see smith-random).

## smith-effects: the model, checked

`smith lint` (and the editor) run the clang-tidy check `smith-effects`, which makes the model above a rule: a program touches the world only through capabilities passed down from `main`, so a function's signature says what it can do. It is on in smith's default `.clang-tidy` (a project that does not want it runs `smith lint --dump-config > .clang-tidy` and deletes `smith-effects` from `Checks`). Everything is decided per function from the source; no file is exempt.

The rules:
1. **A raw effect is a call into a system header that is not on the pure allow list** (`<string.h>`, `<math.h>`, `<ctype.h>`, `<stdint.h>`, `<stddef.h>`, `<float.h>`, `<limits.h>`, `<stdbit.h>`, `<stdckdint.h>`, `snprintf`, `vsnprintf`, `sscanf`, the `strto*` family, `qsort`, `bsearch`, `abs`, `div`, `__builtin_*`), or an Objective-C message to a system framework's class or object (`[NSFileManager defaultManager]`). The list is `shim/effects.h` in smith; a call the list does not name is reported. In a `.smith` file the finding names the callee as written (`c::fopen`).
2. **Failure is not an effect:** `abort`, `assert`, `__builtin_trap`, `__builtin_unreachable`, `smith_failure`, the contract calls and the checks of `<smith/test.h>` are allowed everywhere.
3. **Raw effects are allowed in a capability's implementation.** A capability is a type marked `[[clang::annotate("smith::capability")]]`: SmithIo, SmithConsole, SmithFiles, SmithClock, SmithProcesses, SmithNet and SmithArgs (the process's inputs: the live environment), and the handles SmithOp, SmithCapture, SmithChild and SmithConnection. (SmithKey and SmithArena are values, not capabilities.) Its implementation is the module that defines it: the `.smith` module file, or for C the `.c` file beside the header that defines it (`io.c` with `io.h`; `lib/io.c` or `src/io.c` with a header under the `include/` beside them). Every function there, private helpers included, may make raw calls.
4. **An exported function of that module that reaches a raw effect, directly or through the module's own static functions, must take or return a capability** (or a struct holding one). Otherwise make it `static`.
5. **A main-only constructor may be called only inside `main`:** `smith_io_make`, marked `[[clang::annotate("smith::main_only")]]`. `smith_io_make_test` is not: tests make fakes anywhere.
6. **No capability in static storage:** a global or `static` local of a capability type, a pointer to one, or a struct that holds one.

One option, `smith-effects.CFiles` (default true): `false` makes the check skip `.c` and `.m` files and read `.smith` only (smith's own `.clang-tidy` does this while its C is ported).

**Marking a project's own capability** (a window, a GPU device), in a header or `.smith` module:

```c
typedef struct [[clang::annotate("smith::capability")]] { void *handle; } Window;          // C
export typedef struct [[clang::annotate("smith::capability")]] { void *handle; } Window;   // .smith
```

Raw calls into the platform then go in `window.c` beside `window.h` (or in the `.smith` module), every exported function there takes a `Window *`, and the rest of the program takes a `Window *` like any other capability. A project's constructor that must be called only in main: `[[nodiscard, clang::annotate("smith::main_only")]] Window window_make(...)`.

**Fixing a finding:**

| Finding | Fix |
| --- | --- |
| `fopen`, `open`, `stat`, `rename`... touch the world | take a `SmithFiles *` and call `smith_files_read`, `smith_files_write`... |
| `printf`, `puts`, `fputs(stderr)`, `getchar` | take a `SmithConsole *`: `smith_console_printf`, `smith_console_print`, `smith_console_read_line` |
| `time`, `clock_gettime`, `sleep` | take a `SmithClock *`: `smith_clock_now`, `smith_clock_wall`, `smith_clock_sleep` |
| `fork`, `system`, `popen`, `waitpid` | take a `SmithProcesses *`: `smith_processes_run`, `smith_processes_spawn` |
| `socket`, `connect`, `send` | take a `SmithNet *`: `smith_net_connect`, `smith_net_send`, `smith_net_receive` |
| `malloc`, `free` | take a `SmithArena *`: `smith_arena_push`, or `arena::push` in `.smith` |
| `rand`, `arc4random` | take a `SmithKey` and derive from it (smith-random) |
| `getenv` | `smith_args_env(&io.args, "NAME")` |
| `exit` | return the status from `main` |
| an exported function "reaches `X` but takes no capability" | add the capability parameter it needs, or make it `static` |
| `smith_io_make` "may be called only in main" | take the `SmithIo *` (or the part) as a parameter; a test uses `smith_io_make_test` |
| a capability "cannot be kept in static storage" | pass it as a parameter from `main` |
| an Objective-C message to a framework | wrap the framework in a capability's implementation and take that capability |

## Errors are values

Every operation reports success or a `SmithError`: `code` is 0 or an errno value, `message` says what failed (`open config.txt: No such file or directory`). Nothing is printed behind your back. Only what the program cannot continue without stops it: failing to reserve the arena, a bad `SMITH_SEED`, and the arena's capacity stop.

```c
SmithRead text = smith_files_read(&io.files, &io.arena, "config.txt"); // data is NUL-terminated
if (text.error.code != 0) {
    SmithError printed = smith_console_print_error(&io.console, text.error.message);
    (void)printed;
    return 1;
}
```

## Blocking calls

For simple code: `smith_files_read(files, arena, path)`, `smith_files_write(files, path, data, size, SmithWrite_Replace)`, `smith_console_print(console, text)`, `smith_console_print_error`, `smith_console_read_line(console, arena)`, `smith_clock_now(clock)` (monotonic ns), `smith_clock_wall(clock)`, `smith_clock_sleep(clock, ns)`, `smith_processes_run(processes, arena, argv)` (stdout, stderr, exit status and signal in a `SmithProcessResult`; a missing program is `ENOENT`).

`smith_console_printf(console, format, ...)` and `smith_console_eprintf` format into a 4 KiB buffer and print (`EMSGSIZE` past it). They are not `[[nodiscard]]`: a report of a hundred lines has nowhere to say that stdout closed.

Reading lines to the end of input is a loop with a bound and an end check, which the fake console makes testable:

```c
for (size_t turn = 0; turn < turn_max; turn++) {
    SmithRead line = smith_console_read_line(&io.console, &io.arena);
    if (line.error.code != 0 || line.end) break;   // line.end: the input is over
    ...
}
```

## Write modes, folders, maps, the environment

These block and run at once (they do not queue behind pending asynchronous file operations), on the real io and the fake alike:

| Call | Does |
| --- | --- |
| `smith_files_info(files, path)` | `folder`, `writable`, `size`; `ENOENT` if nothing is there |
| `smith_files_list(files, arena, folder)` | entries (`name`, `folder`, `size`) sorted by name, without `.` and `..` |
| `smith_files_make_folder(files, path)` | like `mkdir -p`; a folder that is there is success |
| `smith_files_write(files, path, data, size, mode)` | writes the bytes as the mode says: `SmithWrite_Replace` (create or replace), `SmithWrite_Atomic` (write `<path>.tmp` and rename it over the path, so nobody sees half a file), `SmithWrite_Append` (add to the end, creating the file) or `SmithWrite_New` (create only if there is none: `EEXIST` otherwise, which is how a program makes a unique name) |
| `smith_files_remove(files, path)` | deletes a file (a folder is `EPERM`) |
| `smith_files_map(files, path)` | maps the file: nothing is read until the memory is touched, so a 150 MB asset costs only the pages used. Private and writable (writes never reach the file), for the life of the program; an empty file is `EINVAL`. The fake gives a copy |
| `smith_args_env(&io.args, "NAME")` | the variable's value, or `nullptr`; the real io asks the process each time |
| `smith_processes_cores(&io.processes)` | how many processes can run at once |

For several arenas with different lifetimes, such as one reset every frame, carve them out of `io.arena` with `smith_arena_carve` (see smith-arena): the 64 GiB reservation commits pages only as they are used.

Reading a large file with `smith_files_read` copies it into the arena; mapping it does not. Choose the map for assets read in place, the read for text.

A program that runs several copies of itself spawns `args.values[0]` with `smith_processes_spawn`, waits with `smith_processes_wait_async` on all of them in one loop, and gets their results from files they wrote with `smith_files_write` and `SmithWrite_Atomic`, in a folder named from `io.key` under `$TMPDIR`; each copy builds what it needs again, since nothing is shared.

## Many processes, each with its output whole

`smith_processes_run` blocks until one process ends. To run several at once and get each one's output when it ends (a build's jobs), start them with `smith_processes_capture_async(&io.processes, &arena, argv, &capture)` and keep the `SmithCapture` where it is. Its stdout and stderr share one pipe, so the lines are in the order the process wrote them, and the process leads a process group of its own (`kill(-capture.process.id, SIGINT)` reaches its children too). After each `smith_io_run`, call `smith_capture_poll(&capture)` for every capture still running: it takes in what the loop completed, submits the next read, and returns true once the process has exited and its output is read to the end. Then `capture.exit_status`, `capture.signal`, `capture.output` and `capture.output_size` are set. Give each capture an arena it can fill: the output grows in the arena by doubling, and when the arena is full the process still runs to its end, `capture.truncated` is set and the end of the output is dropped.

`smith_processes_spawn_terminal` starts a program that talks to the person at the terminal: it keeps this process's stdin, stdout and stderr and stays in this process group, so a Ctrl-C reaches it. (`smith_processes_spawn` gives the program `/dev/null` for stdin.) Wait for it with `smith_processes_wait_async`.

## A child you talk to over pipes

For a program that drives another (a server holding debugger children that speak DAP), `smith_processes_start_piped(&io.processes, argv, &child)` starts `argv[0]` (found on `PATH`; a missing program is `ENOENT`) with a pipe for its stdin and a pipe for its stdout, and returns at once. The child leads a process group of its own. Its stderr is the program's own, not a pipe: a child that logs there can never block on a full pipe nobody reads. The pipes carry raw bytes; framing (lines, `Content-Length:`) is the caller's. Keep the `SmithChild` where it is until `smith_child_close`.

- `smith_child_write_async(&child, &op, data, size)` completes when all the bytes are in the pipe, or fails: `EPIPE` when the child closed its stdin or exited (`op.size` is what was taken first). SIGPIPE cannot kill the program. A write bigger than the pipe (64 KiB) completes as the child reads, so write and read together on one loop and nothing deadlocks. `data` stays yours until done.
- `smith_child_read_async(&child, &op, buffer, capacity)` completes with whatever has arrived (`op.data`, `op.size`, at least one byte, not NUL-terminated), or `op.end` when the child closed its stdout.
- `smith_child_close_input(&child)` closes its stdin, so it sees the end of input (`cat` exits). `smith_child_kill(&child, signal)` signals the whole group (`ESRCH` if there is none). `smith_child_wait_async(&child, &op)` waits for the exit; once that op is done the child's id is free for another program, so `smith_child_kill` returns `ESRCH` and sends nothing (wait with `smith_processes_wait_async(&io.processes, &op, child.process)` and the kill cannot know: then never kill an id after its wait). The same holds for `smith_capture_stop(&capture, signal)`, which signals a capture's group only while its process has not been waited for. `smith_child_close(&child)` closes the pipes still open, once, after the last op is done and the child has exited.
- One write, one read and one wait may be pending on a child at once; several children, the program's own stdin (`smith_console_read_async`), timers and exits all wait in the same `smith_io_run`.

```c
const char *const argv[] = {"smith", "dap", nullptr};
SmithChild child = {};
SmithError error = smith_processes_start_piped(&io.processes, argv, &child);
SmithOp write = {}, read = {}, wait = {};
char buffer[4096];
smith_child_write_async(&child, &write, request, request_size);
smith_child_read_async(&child, &read, buffer, sizeof buffer);
smith_child_wait_async(&child, &wait);
// loop on smith_io_run; resubmit read when read.done and !read.end; finish when wait.done
```

In the fake, script the child first: `smith_io_test_add_child(&io, (SmithChildScript){.program = "smith", .output = "Content-Length: 2\r\n\r\n{}", .chunk = 8, .exit_on = SmithChildExit_InputClosed, .status = 0})`. The first unused script of the program is the child started. It writes its output from the start (in pieces of `chunk` bytes if set); a read with nothing left waits, and ends once the child has exited. `exit_on` says when it exits: `SmithChildExit_Never` (only a kill ends it, with that signal), `_Start` (at once, like `true`: a write is `EPIPE`), `_InputClosed` (like `cat`) or `_OutputRead`. What the program wrote to its stdin is `smith_io_test_child_input(&io, n)` for the n-th script added (8 KiB kept). Up to 16 children.

## Asynchronous calls

To wait on several things in one loop, submit operations in `SmithOp`s you own and run the loop:

```c
SmithOp timer = {}, wait = {};
smith_clock_timeout_async(&io.clock, &timer, 2'000'000'000);
smith_processes_wait_async(&io.processes, &wait, child);
for (size_t round = 0; round < round_max && !(timer.done && wait.done); round++)
    (void)smith_io_run(&io, smith_io_forever);   // or a timeout in ns; 0 only polls
```

`smith_files_read_async`, `smith_files_write_async`, `smith_console_read_async`, `smith_clock_timeout_async`, `smith_processes_wait_async`, `smith_child_write_async` and `smith_child_read_async` submit; `smith_io_run` completes what is ready and sets `op.done`, with `op.error`, `op.data`/`op.size`/`op.end` (reads) or `op.exit_status`/`op.signal` (waits). **Completion is a done flag, not a callback**: the result sits in the op, a loop in your own control flow reads it where your state is, no closure is needed. An op must not move or be freed while pending, and may be reused after done; paths and data it points at stay yours until done.

macOS has no asynchronous file IO, so file operations run synchronously, in submission order, inside `smith_io_run`. Pipes, timers and process exits wait on kqueue; network connections are Network.framework's, whose callbacks run on a dispatch queue and push the finished op onto a lock-free list that wakes the same kqueue (`EVFILT_USER`), so they complete in `smith_io_run` like the rest. Every blocking call is an op run to completion on this same loop.

## The network

`io.net` makes TLS connections with the platform's stack (Network.framework, loaded with `dlopen` on the first connection, so a program that never connects pays nothing at start). A `SmithConnection` is a struct you own and keep in place until its close is done. Everything is an op, with a blocking form:

```c
SmithConnection connection = {};
SmithError error = smith_net_connect(&io.net, &connection, "example.com", 443, true);   // tls
error = smith_net_send(&io.net, &connection, request, request_size);
SmithRead read = smith_net_receive(&io.net, &connection, buffer, sizeof buffer);       // read.size bytes, or read.end
error = smith_net_close(&io.net, &connection);
```

`smith_net_connect_async`, `smith_net_send_async`, `smith_net_receive_async` and `smith_net_close_async` take a `SmithOp` like the other async calls. One send and one receive may be pending on a connection at once. A receive completes with whatever has arrived (at least one byte, at most `capacity`, not NUL-terminated), or `end` when the peer is done. Close completes what is still pending with `ECANCELED` and is the last thing to touch the connection, so free it only after the close is done; a connection that failed is closed like one that worked.

Failures are values: `ECONNREFUSED` and the other errnos of the system, `ENOENT` for a name that does not resolve, and `EAUTH` for a certificate or handshake failure with the reason in the message ("the certificate has expired"). The system's trust decides and there is no way to turn the check off.

## The test io

```c
alignas(smith_arena_align_max) static unsigned char memory[1 << 20];
SmithIo io = smith_io_make_test((SmithIoTest){
    .memory = memory, .capacity = sizeof memory, .seed = 7, .input = "A3 B4\nC5 D6\n"});
smith_io_test_add_file(&io, "config.txt", "volume=3\n");
smith_io_test_add_program(&io, "git", "main\n", 0);
```

It is the same `SmithIo`, so code under test cannot tell it from the real one: files are in memory (writes land there too), the console reads the scripted `.input` then reports the end, output is captured (`smith_io_test_output(&io)`, `smith_io_test_errors(&io)`), the clock moves only when `smith_clock_advance(&io.clock, ns)` says so (and `smith_clock_sleep` of a non-zero time is `EDEADLK`, since nothing can wake it), the key comes from `.seed`, programs are scripted, and its memory is your buffer (a quarter holds file contents). `smith_io_test_add_server(&io, (SmithNetServer){.host, .port, .response, .response_size, ...})` scripts a server of the network: the first connection to `host:port` that no connection has used gets the response (`.chunk` bytes at a time if set), then a clean end or, with `.error`, that errno; `.tls_failure` makes the connect fail with `EAUTH` and that message; a host with no server is `ECONNREFUSED`; `smith_io_test_request(&io, host, port, n)` is what the n-th connection sent. Adding a server to the real io scripts its network only (files, console and clock stay real), which is how code that downloads is tested on a real disk. `SmithIoTest.environment` (`"NAME=value"` entries) and `.cores` give it an environment and a core count; folders exist as soon as a file is under them, or `smith_files_make_folder` made them (a write into a folder that was never made still succeeds, where the real io says `ENOENT`). `smith_io_run` on it never waits: it completes what is ready now. Errors have the same codes and messages as the real io.

Write the function once against capabilities, call it with `&io.files` from main and with the fake's in a test, and compare. The scripted console drives a line loop to completion including the end of input, which is the bug a program that spins on EOF has.

## Get the library in build.c

io uses the arena and random, so compile all three, each with the project's own flags and `-I .smith/include` (see smith-build for the `smith_object` form):

```c
SmithObject io = {.compiler = "smith-cc", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
```

Add `io`, `random` and `arena` to the program's `.objects`.
