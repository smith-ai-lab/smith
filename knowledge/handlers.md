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

The steps (issue #239): the allocator family and its tests (`tests/handlers.sh`); linking it into every program, ASan
before it, and `read`, `write`, `open`, `close`, `syscall` and stdio; `smith::mem` and arena's allocator; `checked`,
`failing` and the profiles.
