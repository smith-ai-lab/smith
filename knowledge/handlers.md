---
type: architecture
title: The handlers runtime
description: One dylib every smith program links whose __interpose tuples send malloc and its family to the allocator of the calling thread; no handler is libc's; built in steps (issue #239).
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
  `verify_interceptors=0` (the program's own strong one wins). The weak one in the dylib is not seen: ASan binds to its own.
- **A thread-local is read in a function of its own**, after the counter and `ready` checks: unoptimized code computes its
  address at the function's start, and dyld calls `close` before the image's thread-local storage exists.

The steps (issue #239): the allocator family and its tests (`tests/handlers.sh`); linking it into every program (this
step, B1); `read`, `write`, `open`, `close`, `syscall` and stdio with their handlers (B2); `smith::mem` and arena's
allocator; `checked`, `failing` and the profiles.
