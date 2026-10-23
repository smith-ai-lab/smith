---
name: smith
description: "The map of smith, a C toolchain in one binary: every verb and which skill covers it. Load when a message starts with smith:, when you run any smith command, or when unsure which smith skill applies."
---

# smith

This skill ships in the smith plugin, from `.agents/skills/smith/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

smith is a C toolchain written in C: clang, clang-format, clang-tidy, clangd, lld and lldb linked into one binary, and a build system whose build file is C or `.smith`. Its top level is only verbs, so `smith foo.c` is an error that says to run `smith cc foo.c`.

## Start here by what you are doing

- Adopting smith in an existing C project (Makefile, CMake, plain `.c` files): `smith-adopt`, the ladder: `CC=smith-cc`, then `smith build --dump-config > build.c`, then tests, contracts and proofs, then `.c` to `.smith` one module at a time.
- Starting a project: `smith new [--language smith|c] [name]` (a `.smith` one by default, with no build file and no config file). Then `smith run`, `smith test`, `smith check`.
- Changing what the build does: `smith-build-levels` (conventions, `--dump-config`, fields, tasks, the low-level API), then `smith-build` (every call).
- Writing, editing, reviewing or porting a `.smith` file: **`smith-idiom` first** (sized types, `import` and `c::`, modules that import modules, generics, no preprocessor, capabilities; a porting checklist with counts), then `smith-lang` for the language. A `.smith` file that is C renamed is not done. The libraries are below; each leads with its `.smith` use.

## Every verb

- `smith cc`, `smith c++`, and the links `smith-cc` and `smith-c++`: the compiler. Load `smith-cc` (a Makefile's `CC`, compile errors, `compile_commands.json`, `SMITH_VERBOSE`); shaders: `smith-metal`.
- `smith build`, and a project's `build.c` or `build.smith`, and turning a Makefile into one (`smith build --dump-config > build.c`): load `smith-build`, and `smith-build-levels` to move between the conventions, the high-level `smith_app` and the low-level calls.
- `smith run`, `smith test`: run the program `smith_default` names, run the tests `smith_tests` declares. Load `smith-run-test`; `smith-test` for writing tests, property-based tests and `smith::test` (`<smith/test.h>` from C).
- `smith check`, `smith fmt`, `smith lint`: format (every file the repository tracks or could track), lint (the build's compiles), all checks (fmt, docs, lint, test, prove). Load `smith-check`. The verbs mean the same in every project and need no task in build.c; a project's own tasks run with `smith build <task>`. `smith fmt --dump-config > .clang-format` and `smith lint --dump-config > .clang-tidy` take the defaults over.
- `smith lsp`: the language server for C, C++, Objective-C and `.smith` (any editor, Claude Code). Load `smith-lsp`; for Neovim `smith-neovim`.
- `smith docs`: checks AGENTS.md, knowledge/ (an OKF bundle) and the skills of a project, and says the format. Load `smith-docs`.
- `smith prove`: contracts (`smith_requires`, `smith_ensures`, `smith_invariant`, `smith_assert`) and their proofs. Load `smith-contract`.
- `smith dbg`, `smith dap`: debugging at a terminal and in an editor (`smith-debug`); `smith mcp`: the debugger as tools for an agent (`smith-mcp`).
- `smith symbolize`, `<smith/trace.h>`, a log with `smith-trace` lines, sanitizer reports: `smith-trace`.
- `smith fetch`, `smith cache`, `smith_package`: packages and the caches: `smith-packages`.
- `smith new`: above. `smith version`: prints the version and, after `commit`, the commit smith was built from.
- `.smith` files, C with modules; what to write in them is `smith-idiom` (load it first). The language (`module a::b;`, `import`, `export`, `c::printf`, `u32`): load `smith-lang`, then `smith-lang-modules`, `smith-lang-c-interop`, `smith-lang-sized-types` or `smith-lang-generics` (`constexpr type T`, `if constexpr`).
- Libraries (each shows `.smith` first, C under "From C"): `smith-arena`, `smith-autodiff` (Enzyme's derivatives; autodiff programs build with full LTO, and ASan but no UBSan in debug), `smith-inflate` (DEFLATE: gzip, zlib, raw), `smith-io`, `smith-random`, `smith-math`, `smith-text`, `smith-json`, `smith-test`, `smith-contract`, `smith-trace`. The build API as a module (`import smith::build;`) is in `smith-build`.

If smith is missing from PATH or seems broken, stop and tell the owner. Never swap in another compiler to get around a problem; rerun with `SMITH_VERBOSE=1` and report the command and its output.
