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

## Asynchronous calls

To wait on several things in one loop, submit operations in `SmithOp`s you own and run the loop:

```c
SmithOp timer = {}, wait = {};
smith_clock_timeout_async(&io.clock, &timer, 2'000'000'000);
smith_processes_wait_async(&io.processes, &wait, child);
for (size_t round = 0; round < round_max && !(timer.done && wait.done); round++)
    (void)smith_io_run(&io, smith_io_forever);   // or a timeout in ns; 0 only polls
```

`smith_files_read_async`, `smith_files_write_async`, `smith_console_read_async`, `smith_clock_timeout_async` and `smith_processes_wait_async` submit; `smith_io_run` completes what is ready and sets `op.done`, with `op.error`, `op.data`/`op.size`/`op.end` (reads) or `op.exit_status`/`op.signal` (waits). **Completion is a done flag, not a callback**: the result sits in the op, a loop in your own control flow reads it where your state is, no closure is needed. An op must not move or be freed while pending, and may be reused after done; paths and data it points at stay yours until done.

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
SmithObject io = {.compiler = "smith", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
```

Add `io`, `random` and `arena` to the program's `.objects`.
