---
name: smith-new
description: "smith new [--language smith|c] [<name>]: starting a project with a program, tests and the agent documents, and no build or config file. Load when asked to start, create or scaffold a C or .smith project, or on what smith new writes."
---

# smith new

`smith new [--language smith|c] [<name>]` writes a ready project into `<name>` (or the current folder) and never overwrites a file. `smith` is the default language. What it promises is in [smith-new](../../../knowledge/smith-new.md).

| `--language smith` (default) | `--language c` |
| --- | --- |
| `main.smith` (`module main;`), `primes.smith` (a module; no header), `tests/primes_test.smith` | `main.c`, `primes.c`, `primes.h`, `tests/test_primes.c` |

Both also get:

- `.gitignore`.
- `AGENTS.md`, for the agents that will write the project: rules, one line each.
- `knowledge/` with an `index.md` and a first concept, `program.md`, that says what the program promises.
- `.agents/skills/<name>-docs/SKILL.md`, the project's guide to its own docs, and the `.claude/skills` link to `.agents/skills` ([smith-docs](../smith-docs/SKILL.md)).
- `.claude/settings.json`, which enables the smith plugin and declares its marketplace, so smith's skills are not copied into the project.

It writes **no** `build.c` or `build.smith`, no `.clang-format` or `.clang-tidy`, and no root `compile_commands.json`: smith's conventions and defaults cover them ([smith-build](../smith-build/SKILL.md), [smith-check](../smith-check/SKILL.md)). `smith build --dump-config`, `smith fmt --dump-config > .clang-format` and `smith lint --dump-config > .clang-tidy` make any of them the project's when you want to change one.

Then it runs a first build and commits on `main`. A folder that already has its own build file, a `main`, or another build system keeps its program, so `smith new` adds only what agents need.

The program is the example of idiomatic Smith ([smith-idiom](../smith-idiom/SKILL.md)): a contract the prover proves, a test, and a `main` that takes the io.

Next: `smith run`, `smith test`, `smith check` ([smith-run-test](../smith-run-test/SKILL.md)).
