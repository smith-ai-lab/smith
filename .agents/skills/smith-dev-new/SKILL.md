---
name: smith-dev-new
description: "The files smith new writes: templates/new, its placeholders, the defaults embedded for fmt and lint, and the test that keeps them in step. Load before changing smith new's project or smith's default .clang-format and .clang-tidy."
---

# smith-dev-new

What smith new promises is in [smith-new](../../../knowledge/smith-new.md).

- The files `smith new` writes live in `templates/new/` (dotfiles without their dot; `agents.md`, `knowledge-index.md`, `knowledge-program.md` (the `.smith` project's concept; `knowledge-program-c.md` is the C one's) and `docs-skill.md` are `AGENTS.md`, `knowledge/index.md`, `knowledge/program.md` and `.agents/skills/<name>-docs/SKILL.md`, where `@NAME@` is the program's name and `@KEBAB@` the same as a skill's name; `.claude/skills` is a link made in `smith.c`; `main.smith`, `primes.smith` and `test_primes.smith`, or `main.c`, `primes.c`, `primes.h` and `test_primes.c`, are the program, one set for each `--language` (a row of `smith_new_files` says which), and `smith.c` leaves them out when the project has its own build file, another build system or a `main` of the other language) and are embedded with `#embed`. There is no build file and no config file among them: smith builds by convention, and smith's own `.clang-format` and `.clang-tidy`, which `smith fmt` and `smith lint` use where a project has none, are `templates/defaults/clang-format` and `clang-tidy`. The skills are not among them: a project gets smith's skills from the smith plugin, which the `.claude/settings.json` that `smith new` writes enables. A test keeps the defaults' rules equal to smith's `.clang-format` and `.clang-tidy`; `tests/new_test.smith` runs with `smith test`.
