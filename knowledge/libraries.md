---
type: convention
title: smith's libraries are a modern standard library for C
description: A library joins when two projects need it, does one thing, uses standard types, defines no macros, and is compiled from source by the project through smith_library.
tags: [libraries, stdlib]
---
# smith's libraries are a modern standard library for C

**smith's libraries are a modern standard library for C, holding only what is proven.**
- **Joining:** a library joins when two projects need the same thing (game's trigonometry and random's logarithm make
  `<smith/math.h>`), never on speculation. So there are no vectors or tensors until a second project needs one.

**What one looks like:**
- It does one thing and is named for it, so there is no "base" or "utils".
- It uses standard types: `<stdint.h>`, `size_t`, `float` and `double`, with no aliases.
- It uses the standard `assert` and defines no macros: a library's API is functions (and, in `.smith`, generics).
  The check macros of `<smith/test.h>` were the last ones, and are gone with test a module (#45). The reason for
  them, capturing a check's expression text, was gone: a failure's report reads the failing line from the call
  stack.
- Its names are typed as `<math.h>` names them, with no `_Generic`.

**How a project uses one:** it compiles the library from source with its own flags (`smith_library`).

**C or `.smith`, one registry:** a library is its header (`include/smith/<name>.h`) and its source, `lib/<name>.c`
or a `.smith` module `lib/smith/<name>.smith` (`module smith::<name>;`), one row of `smith_libraries` in
`smith.c`. A `.smith` library's header is generated and committed (the stage0 has no hook and embeds it as
bytes), `smith build headers` regenerates it and `check-headers` (in `check`) fails on a difference; its C API
(names, symbols via `[[smith::c_name]]`, doc comments) stays what it was. `text`, `random`, `arena`, `test` and `io` are `.smith` (issue #39; #45). C passes `sizeof` and
`alignof` to `smith_arena_push`; `.smith` writes the type as an argument, `arena::push(&a, T)` and
`arena::push_slice(&a, T, sizes...)` (a slice with a size for each `[:]`).

**Talking to the OS:** `smith::io` (`import smith::io;`, `<smith/io.h>` from C) is the one way a program touches the
world outside it (memory, randomness, console, files, clock, processes, the network), each platform behind it on its
best API (kqueue and Network.framework on macOS). Its module names are the C names without the prefix (`io::Io` is
`SmithIo`, `io::console_printf` is `smith_console_printf`); no nested modules, so a capability's functions are
`io::<capability>_<name>`.
