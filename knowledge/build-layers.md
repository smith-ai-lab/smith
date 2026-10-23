---
type: design
title: Two layers of the build API
description: The high-level smith_app calls and the low-level object and program calls are one API at two levels.
tags: [build, api]
---
# Two layers of the build API

build.c has two layers, and a project may use either or both.

- **The raw layer** (`smith_step`, `smith_task`) is the make replacement: you own every command, and smith knows
  nothing about what a step does. It cannot tell a compile from an `rm`.
- **The high level** (`smith_object`, `smith_program`, `smith_compile_command`, `smith_compile_commands`) is for letting smith know what a
  compile and a link are. Each call emits ordinary steps, the same as `smith_step`, and its comment in the header states
  the exact command. `smith build -v` shows it. Nothing is hidden; the gain is that smith can write a correct
  `compile_commands.json`, which clang's `-MJ` cannot (it records clang's internal command, `-cc1`, which clangd
  rejects), and which has an entry for build.c itself so an editor finds `<smith/build.h>`.

`compile_commands.json` rules:
- One entry per `smith_object`, in call order: `directory` is the project root, `file` the source, `output` the object,
  and `arguments` the exact argv of the step, `-MMD -MF` included (clangd tolerates them, and the file tells the truth).
  An object declared again with the same output has one entry.
- Then one entry per `smith_compile_command(b, compiler, source, flags)`, in call order: `file` the source, no `output`,
  and `arguments` the compiler, the flags, `-c` and the source. It is for files the build never compiles on their own,
  such as sources reached only through a unity build, so the editor still knows their flags. A source that already has
  a `smith_object` entry or an earlier `smith_compile_command` is skipped (the first entry for a file wins), and
  calling it after `smith_compile_commands` is an error.
- Then one entry for build.c: `file` "build.c" and `arguments` `smith-cc -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include
  -c build.c`, the flags `smith build` compiles build.c with (a test checks they match smith.c's bootstrap).
- The JSON is escaped (`"`, `\`, control characters as `\u00XX`), indented two spaces, ends with a newline, and is
  written only when its bytes change, through `smith_write`'s rules. A `.run` step does not run build(), so it writes nothing there.
- Calling `smith_compile_commands` twice is an error, and so is a path under `.smith/`. Call it last.
- A missing required field is an error naming it; so is an object in `.objects` that was never passed to `smith_object`.
  A program's step lists the objects' outputs as inputs and the libraries that are files (a path with `/`, or ending in
  `.a`, `.dylib` or `.so`, and not starting with `-`) as implicit inputs; `-framework X` is not a file.
