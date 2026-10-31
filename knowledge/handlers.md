---
type: architecture
title: The handlers runtime
description: One runtime every smith program links (a dylib with __interpose tuples on macOS, an object defining the functions on Linux) that sends malloc and its family to the allocator of the calling thread; no handler is libc's; built in steps (issue #239).
tags: [handlers, interposition, allocator]
---
# The handlers runtime

Code calls effects directly (`malloc`, later `read`, `write`, stdio), and a test, an arena or a fake must be able to stand
in for them, including in third-party C that smith never sees the source of. So one runtime, `runtime/handlers.c` (a
dylib), carries `__interpose` tuples that replace libc's functions in every image of the process, the program's, a
prebuilt library's and one loaded with `dlopen`. Owner (2026-10-24): "always intercept. Measure."

- **The handler is the calling thread's** allocator, a `SmithAllocator` (`include/smith/handlers.h`: `alloc`, `realloc`,
  `free`, `owns`, and an optional `size` for `malloc_size`), set with `smith_handlers_set_allocator`, which returns the
  previous one as `signal()` does; restoring it is the caller's (`defer` in smith). None set is libc's own function, a
  relaxed load of a counter on the way.
- **The runtime calls libc by its names**, not through `dlsym`: a dylib's own calls are not interposed by its tuples, so
  there is no bootstrap buffer; a `ready` flag, set by a constructor, keeps the thread-local read away from the allocations
  dyld and ASan make before the image's thread-local storage exists.
- **`free` and `realloc` ask `owns`**: a block that is not the handler's (made before it was set, or by libc) goes to libc,
  so a handler can come and go around a program that holds both kinds. The `malloc_zone_` family is one passthrough: a
  block of the handler answers whatever zone is named, `malloc_zone_from_ptr` answers the default zone for it, the rest is
  the zone's own (CoreFoundation and Objective-C ask the zone about a pointer and free through it).
- **An arena set around code whose memory outlives it is the hazard**: stdio takes its buffer from the first handler in
  force and keeps it past the scope, which `tests/handlers/main.c` met (it gives stdout a buffer of its own). `checked`
  and the effects check are the later steps.

- **Linking (step B1):** `lib/handlers.c` is embedded like a library (`smith_libraries` in smith.c, `.smith/lib/handlers.c`,
  `.smith/include/smith/handlers.h`) and `smith_program` (runtime/build_runtime.c) links it into every program on macOS: one
  step per program folder makes `libsmith_handlers.dylib` (`-dynamiclib`, never sanitized, `-install_name
  @rpath/libsmith_handlers.dylib`), the program's link gets `-L<folder> -lsmith_handlers` and the rpath `@executable_path`
  (a sanitized link already has it), so the dylib travels with the program. A program that includes `<smith/handlers.h>`
  does not link the source as an object (`smith_app_library` skips it): the functions are the dylib's.
- **ASan:** a program with `-fsanitize=address` is linked with `-lclang_rt.asan_osx_dynamic` before `-lsmith_handlers`, so
  the runtime's tuples sit ahead of ASan's and a call with no handler reaches ASan's allocator (spike 4 of #208, option A).
- **smith itself is the exception** (its link is named `.../smith`, `smith_is_smith_itself`): its clones, `bin/clang` and
  `bin/ld`, run from the programs folder, which has no dylib beside them. A field of `SmithProgram` replaces the name once a
  release's header has it (the lag rule).
- **Linux (issue #305)** has no interpose tuples, but an ELF program's own definitions win over a shared library's: the
  runtime is an object, `<folder>/smith_handlers.o` (one step per folder, never sanitized), linked into every program
  but smith, and `lib/handlers.c`'s Linux half defines `malloc`, `write`, `fwrite`... themselves. Each calls the next
  definition: ASan's `___interceptor_<name>` (its `malloc` and the rest are weak aliases of those) when the program is
  sanitized, else glibc's, `__libc_malloc` and its family for memory and `dlsym(RTLD_NEXT)` for the rest, so nothing in
  the file calls a function it defines. Streams are `fopencookie`'s; `getchar`, `putchar`, `vprintf` and `getline`,
  which glibc's `<stdio.h>` makes extern inline when optimized, are defined under those names by asm labels, and
  `getdelim`/`__getdelim` (an optimized `getline`) are the stream's too. `malloc_usable_size` stands for `malloc_size`;
  there are no zones. A sanitized program also links an `__asan_default_options` (strong: the runtime's weak one is first in the link) of `abort_on_error=1:detect_leaks=0`, so a report
  stops with SIGABRT as on macOS and the leak check Linux's ASan adds at exit is off, as on macOS. A program built with `_FORTIFY_SOURCE` calls `__printf_chk` and its kind (`__fprintf_chk`, `__vfprintf_chk`, `__dprintf_chk`, `__fgets_chk`,
  `__fread_chk`, `__read_chk`...); the runtime defines them as their plain forms, and with no handler set they call glibc's own `_chk` (ASan's when sanitized).
- **An optimizer knows malloc**: it may drop or fold a malloc/free pair, so a handler is seen only through calls it cannot
  see through (`tests/handlers/project.c` keeps its pointers in `volatile`).

- **Files and console (step B2):** thirty functions are interposed besides the allocator's: `read write open close
  syscall` (only `SYS_read` and `SYS_write` are decoded) and the stdio family (`fwrite fread fgets fgetc getc getchar
  ungetc fputs fputc putc putchar puts printf fprintf vprintf vfprintf dprintf vdprintf fflush fopen fdopen freopen fclose
  fscanf scanf getline fseek`). `SmithConsoleHandler {write, read}` takes fds 0, 1 and 2 and `SmithFileHandler {open, read, write,
  close}` every descriptor from 3 up, each set per thread with a setter that returns the previous handler. With none set
  a counter says so and the real function runs. With a files handler set, `fopen`, `fdopen` and `freopen` return a
  `funopen` stream whose callbacks call the handler, so C's `FILE *` I/O is faked too (not seekable). Not reached: raw
  `svc`, `pread`, `readv`, `send` and `recv` (the task runtime, #241); `getpid` is left alone (interposing it aborts
  Objective-C, spike 4).
- **`puts` and ASan:** ASan's start-up check wants `dlsym("puts")` to land in its own runtime (`VerifyInterceptorsWorking`,
  `verify_interceptors`), which our `puts` tuple makes false, so every ASan program died with "interceptors not installed".
  A program linked with ASan therefore also links `smith_asan_options.o`, a weak `__asan_default_options` returning
  `verify_interceptors=0` (the program's own strong one wins). The weak one in the dylib is not seen: ASan binds to its own. Turning the check off
  hides nothing: ASan's interceptors are still in front of ours (our calls to libc functions reach ASan's, `tests/handlers_test.smith`
  checks a heap overflow, a use after free and a `puts` that reads past its string, all still reported), and the check only
  asks about `puts`, whose tuple is ours on purpose.
- **A thread-local is read in a function of its own**, after the counter and `ready` checks: unoptimized code computes its
  address at the function's start, and dyld calls `close` before the image's thread-local storage exists.

- **The arena as an allocator (step C1):** `arena::allocator(&a)` (C: `smith_arena_allocator`) returns an `ArenaAllocator`
  whose `.base` is the `SmithAllocator` to pass to `smith_handlers_set_allocator`; the arena must outlive it and the value
  must not move while set. Every block has a 32-byte header (size, the cursor after it, the header size), starts 16-aligned
  (or at the request, up to 64), a request that does not fit is null as malloc's (never the stop of a push), `owns` is a
  range check, `free` of the last block gives it back (no scope open), `realloc` of a growing block copies. The struct is
  a container with the `SmithAllocator` first, not a `ctx` field, because a field added to handlers.h waits for a smith
  release (the lag rule: arena is built into smith).

- **Refusing (step D2b):** `failing::make(parent, key)` (smith::failing) refuses allocations by draws from a random key, and the test
  runner gives a test one beside its key; `.checked_allocator` turns the runner's checked allocator off. See
  [checked-allocator](checked-allocator.md).

- **Checking (step D2a):** `mem::checked(parent)` wraps an allocator and finds leaks, double frees and use after free; the
  test runner sets one around each test. See [checked-allocator](checked-allocator.md).

The steps (issue #239): the allocator family and its tests (`tests/handlers_test.smith`); linking it into every program (this
step, B1); `read`, `write`, `open`, `close`, `syscall` and stdio with their handlers (B2); `smith::mem` and arena's
allocator; `checked` (D2a), `failing` and the profiles (D2b).

- **Allocation in .smith (step C2, owner 2026-10-25):** `smith::mem` (`lib/smith/mem.smith`) is the smith face of the
  allocator handler: `mem::set_allocator` returns the previous one (`defer` puts it back), `mem::alloc(T)` and
  `mem::alloc_slice(T, n)` go through whatever `malloc` reaches and never return null, `mem::try_alloc` and `mem::try_alloc_slice` return an
  `option`, `mem::realloc(xs, n)` and `mem::try_realloc(xs, n)` resize a slice through libc's `realloc` (so the handlers see it) for code that refuses, and the `malloc` itself keeps C's nullable contract. A refusal is a checked failure that
  names the size, for the cases "if you can handle it locally it is a result or an option, otherwise you let it crash":
  allocation is the second kind.
- **Arenas grow or are fixed.** `arena::growing(first, backing)` takes a chunk twice the current block (or what the push
  needs) from its backing (the OS through `mmap` by default, or a `SmithAllocator`) when full, so `push` and `push_slice`
  never return null; `Arena.grown` is the bytes beyond the first chunk; chunks are a list kept across `reset` and the end
  of a scope (a scope remembers the chunk it began in, ending it walks back and keeps the later ones as spares), so the
  second pass over the same work takes none; `arena::release` gives them back. `arena::fixed(memory, capacity)` never grows: a full one is the checked failure that names the capacity. `arena::try_push_raw` is the form that
  returns null. As an allocator (`arena::allocator`) a growing arena answers `owns` for every chunk and null only when
  its backing refuses. `arena::try_push` is the `option` form of `push` (`.none` instead of the stop or the checked failure, nothing
  pushed), built on `try_push_raw` as `mem::try_alloc` is on `try_alloc_raw`; a slice has no `option` form yet.

- **Environment and clock (issue #240):** `SmithEnvHandler {get}` answers `getenv`; `SmithClockHandler {now, sleep}` (nanoseconds)
  answers `clock_gettime`, `nanosleep`, `usleep` and `sleep`. Each is per thread with a setter that returns the previous
  handler, and none set is libc's. `gettimeofday`, `time` and the Mach clocks are not reached. A fake clock's `sleep`
  advances its time and returns, so sleeping on it never blocks. See [direct-call-modules](direct-call-modules.md).
- **Processes (issue #240):** `SmithProcessHandler {start, read, write, close_input, wait, kill, close, cores}` is called by `smith::process` itself (no libc function is interposed); per thread, with a setter returning the previous handler, and none set is the real posix_spawn, pipes and waitpid. See [process](process.md).
- **Network (issue #240):** `SmithNetHandler {connect, listen, port, accept, read, write, close, read_within}` is called by `smith::net` itself (no libc function is interposed); per thread, with a setter returning the previous handler, and none set is the real sockets and TLS; `read_within` is a read with a deadline (-1 with `ETIMEDOUT`), and a handler that leaves it null gets `read`. See [net](net.md).
- **Entropy (issue #240):** `SmithEntropyHandler {fill}` answers `getentropy` (per thread, setter returns the previous
  one, none set is the system's; `fill` writes the bytes and never fails). `arc4random` and `getrandom` are not reached.
  `entropy::seed()` (`smith::entropy`) is the one caller in smith's libraries; it has the `entropy` effect
  (`getentropy` is `entropy` in `shim/effects_table.h`), and `smith::random` stays pure. A test scripts it with
  `entropy::fake(number)`, whose `fill` writes the bytes of a number.

- **One table per thread (issue #885):** the handlers of a thread are one `SmithHandlers` (`include/smith/handlers.h`: a
  pointer per kind, `allocator`, `files`, `console`, `env`, `clock`, `entropy`, `net`, `process`). Each thread has a table
  of its own and one `thread_local` pointer names the current table (null: its own); `smith_handlers_table()` returns it and
  `smith_handlers_set_table(table)` makes another current and returns the previous one (null: the thread's own). Every
  setter and getter keeps its name and meaning: it reads or writes its member of the current table, so a thread that never
  swaps behaves as before. The per-kind counters count the non-null handlers of the tables that are current (a swap moves
  them), so the fast path is still one relaxed load, and the thread-local is still read in a `noinline` function after
  the counter and `ready` checks. The task runtime (#241) gives each task a table and swaps it at a task switch: one store
  instead of eight. `tests/handlers/table.c` tests the swap and two threads; `tests/handlers/bench.c` prints the cost of a
  malloc/free pair (Linux x86_64, -O2, best of five): no handler 11.4 ns before and 11.5 ns after; under a bump handler
  6.2 ns before and 6.6 ns after; the bump pair under ASan 6.7 ns before and 7.0 ns after (no handler under ASan 161 ns
  both).
