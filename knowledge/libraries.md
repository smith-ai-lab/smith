---
type: convention
title: smith's libraries are a modern standard library for C
description: smith is batteries included, so a project needs no third-party library; each library does one thing, uses standard types, defines no macros, and is compiled from source by the project through smith_library.
tags: [libraries, stdlib]
---
# smith's libraries are a modern standard library for C

**smith is batteries included** (owner, 2026-10-25): its standard library covers what programs commonly reach for
(data formats and serialization, ML, windows and input, the GPU, HTTP), so a project never needs a third-party library.
- **Joining:** a library joins when programs commonly need it. Its first user is usually a real project (the game or
  the learner), whose code it replaces; its design comes from that use, and its tests cover it.

**What one looks like:**
- It does one thing and is named for it, so there is no "base" or "utils".
- It uses standard types: `<stdint.h>`, `size_t`, `float` and `double`, with no aliases.
- It uses the standard `assert` and defines no macros: a library's API is functions (and, in `.smith`, generics).
  The check macros of `<smith/test.h>` were the last ones, and are gone with test a module (#45). The reason for
  them, capturing a check's expression text, was gone: a failure's report reads the failing line from the call
  stack.
- Its names are typed as `<math.h>` names them, with no `_Generic`.

- **A library that allocates is its own module** (#240): `smith::random` is linked into smith itself and imports nothing that allocates, so the categorical distribution, whose table is built with `mem::alloc`, is `smith::categorical` (imports `random` and `mem`), which only user programs link; the lag rule and smith's link would otherwise force `mem` and `handlers.c` into smith.

- **Errors follow one line** (owner, 2026-10-25, #327): what the caller can handle locally is a value (`option`, `result`, a status: bad input, a limit, a full buffer, a missing file); misuse and exhaustion panic with a report (a wrong call, an arena or a buffer the library may not outgrow). `json` and `inflate` follow it; the others are being moved.

**How a project uses one:** it compiles the library from source with its own flags (`smith_library`).

**C or `.smith`, one registry:** a library is its header and its source, `lib/<name>.c`
or a `.smith` module `lib/smith/<name>.smith` (`module smith::<name>;`), one row of `smith_libraries` in
`smith.c`. A C library's header is hand-written (`include/smith/<name>.h`). A `.smith` library's header is a build
output, never committed ([smith-module-headers](smith-module-headers.md)): smith embeds the source and the installed
smith writes the header into `.smith/include/smith/` at first use; its C API (names, symbols via `[[smith::c_name]]`,
doc comments) stays what it was. `option` and `result` (issue #198) are `.smith` modules, `smith::option` and `smith::result`, that export only a type function each; they are no library to link and are never imported: the lowering carries their `// smith-eshape:` lines itself (`runtime/lang_enum.c`) so that `option(T)` and `result(T, E)` are in scope in every module. `text`, `random`, `arena`, `test` and `io` are `.smith` (issue #39; #45). C passes `sizeof` and
`alignof` to `smith_arena_push`; `.smith` writes the type as an argument, `arena::push(&a, T)` and
`arena::push_slice(&a, T, sizes...)` (a slice with a size for each `[:]`).

**Talking to the OS:** `smith::io` (`import smith::io;`, `<smith/io.h>` from C) is the one way a program touches the
world outside it (memory, randomness, console, files, clock, processes, the network), each platform behind it on its
best API (kqueue and Network.framework on macOS, epoll and OpenSSL on Linux). Its module names are the C names without the prefix (`io::Io` is
`SmithIo`, `io::console_printf` is `smith_console_printf`); no nested modules, so a capability's functions are
`io::<capability>_<name>`.
