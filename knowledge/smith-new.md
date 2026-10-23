---
type: design
title: smith new writes the program and what agents need
description: smith new writes a program, its tests and the agent documents, with no configuration, and builds and commits once.
tags: [new, agents]
---
# smith new writes the program and what agents need

**`smith new [--language smith|c] [<name>]` writes the program and what agents need, and no configuration**
(issue #43; `smith` is the default). The program is `main.smith`, `primes.smith` (a module; no header) and
`tests/test_primes.smith`, or with `--language c` `main.c`, `primes.c`, `primes.h` and `tests/test_primes.c`; with
`.gitignore`. For agents: `AGENTS.md`, `.claude/settings.json` (enables the smith plugin, so smith's skills are not
copied), `knowledge/` (an index and a first concept) and the docs skill `.agents/skills/<name>-docs` with the
`.claude/skills` link. The owner: "we have to assume the primary author is going to be Claude or some other agent."

**No `build.c` or `build.smith`, no `.clang-format` or `.clang-tidy`, no root `compile_commands.json`:** smith's
defaults cover them (conventions for the build; the embedded style and checks for `smith fmt` and `smith lint`;
`.smith/compile_commands.json` for the editor). `--dump-config` makes any of them the project's.

**Git on `main`, a first build and one commit** stay; a folder that already has its own build file, a `main`, or
another build system keeps its program, so `smith new` adds only what agents need.

`smith new [--language smith|c] [<name>]` writes a ready project into `<name>` (or the current folder), never
overwriting: `main.smith` (`module main;`), `primes.smith` and `tests/primes_test.smith` (`module primes_test;`, each test exported) (or `main.c`, `primes.c`, `primes.h` and
`tests/test_primes.c`), `.gitignore`, `AGENTS.md`, `knowledge/` (an index and a first concept),
`.agents/skills/<name>-docs` with the `.claude/skills` link, and `.claude/settings.json` (which enables the smith
plugin and declares its marketplace), then builds once and commits on `main`. No build file, no config file, no
skills copied. `tests/new_test.smith` tests it.
