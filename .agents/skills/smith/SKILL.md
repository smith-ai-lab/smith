---
name: smith
description: "The map of smith, a C toolchain in one binary: every verb and which skill covers it. Load when a message starts with smith:, when you run any smith command, or when unsure which smith skill applies."
---

# smith

This skill ships in the smith plugin, from `.agents/skills/smith/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

smith is a C toolchain written in C: clang, clang-format, clang-tidy, clangd and lld linked into one binary, and a build system whose build file is C. Its top level is only verbs, so `smith foo.c` is an error that says to run `smith cc foo.c`.

- `smith cc`, `smith c++`, and the links `smith-cc` and `smith-c++`: the compiler. Load `smith-cc` (a Makefile's `CC`, compile errors, `.metal`, `compile_commands.json`, `SMITH_VERBOSE`).
- `smith build`, and a project's `build.c`: load `smith-build`.
- `smith run`, `smith test`: run the program `smith_default` names, run the tests `smith_tests` declares. Load `smith-run-test`; `smith-test` for writing tests.
- `smith check`, `smith fmt`, `smith lint`, `smith lsp`: format (every file git sees), lint (the build's compiles), all checks (fmt, docs, lint, test, prove), the editor. Load `smith-check`. The verbs mean the same in every project and need no task in build.c; a project's own tasks run with `smith build <task>`.
- `smith docs`: checks AGENTS.md, knowledge/ (an OKF bundle) and the skills of a project, and says the format. Load `smith-docs`.
- `smith prove`: load `smith-contract`. `smith dbg` says it is not available yet.
- `smith new`, `smith fetch`, `smith cache`: start a project, fetch a package, show and clean the caches (the smith-build skill).
- `smith version`: prints the version and, after `commit`, the commit smith was built from.
- `.smith` files, C with modules (`module a::b;`, `import`, `export`, `c::printf`, `u32`): load `smith-lang`, then `smith-lang-modules`, `smith-lang-c-interop`, `smith-lang-sized-types` or `smith-lang-generics` (`constexpr type T`, `if constexpr`).
- Libraries: `smith-arena`, `smith-io`, `smith-random`, `smith-text`.
- If smith is missing from PATH or seems broken, stop and tell the owner. Never swap in another compiler to get around a problem; rerun with `SMITH_VERBOSE=1` and report the command and its output.
