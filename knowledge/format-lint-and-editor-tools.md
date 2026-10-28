---
type: architecture
title: clang-format, clang-tidy and clangd, from smith's LLVM
description: smith fmt, lint and lsp are the LLVM tools linked into smith, with smith's defaults where a project has no file, and .smith is C to clang-format.
tags: [format, lint, lsp, defaults]
---
# clang-format, clang-tidy and clangd, from smith's LLVM

**`smith fmt`, `smith lint` and `smith lsp` are clang-format, clang-tidy and clangd, from the LLVM smith compiles
with** (linked into smith; the stage0 runs Homebrew's, or `$SMITH_CLANG_FORMAT`, `$SMITH_CLANG_TIDY`,
`$SMITH_CLANGD`), so formatting, lint and the editor are the same everywhere, and each takes the tool's own arguments.
Point an editor at `smith lsp` with no `--query-driver`: with it clangd puts the SDK's include paths ahead of
clang's own headers, and Apple's SDK module map refuses that order, so it fails on modules.

**smith's defaults where a project has no file** (issue #43): a file with no `.clang-format` or `.clang-tidy` up its
tree (the tools' own lookup) gets smith's, embedded from `templates/defaults/` and written to `<data>/smith/defaults/`,
passed as `--style=file:` and `--config-file=` (`smith_defaults_apply`, smith.c). A project's file replaces them
whole and an explicit `--style`, `--config` or `--config-file` wins. `--dump-config` is the tool's, so it prints the
effective one and makes it the project's (`smith fmt --dump-config > .clang-format`). `smith cc` is untouched.

**Project `smith lint` is a gate over every `.smith` source** (issue #424, `smith_lint_project`, runtime/verbs.c): the
build's compiles (with their own command from `.smith/compile_commands.json`) and every other `.smith` file git sees
that `.clang-format-ignore` does not name (the one list of test inputs, generated and copied files; clang-tidy reads the
nearest compile's command for it). A finding fails it, the last line is `smith lint: linted N files (R run, C cached, T s)` (`smith check`:
`lint ok: N files`), and a project with `.smith` sources that lints none fails. A stage0, whose clang cannot read
`.smith`, leaves them out and says so.

**`smith lint` is incremental** (issue #621, `runtime/lint_cache.c`, [the caches](caches.md)): clang-tidy's output and exit
status for a file are kept under `<cache>/smith/lint` and replayed when nothing the lint read has changed, so a lint
of an unchanged tree runs clang-tidy on no file (about 1 s for smith's 180 files, from 50 s) and one changed file runs
only itself and the files that include it. `SMITH_LINT_CACHE=0` turns it off, `SMITH_LINT_CACHE=why` says why a file
ran. Tests: `tests/lint_cache_test.smith`.

**`smith lint --changed[=<ref>]` lints what a change reaches** (issue #638, `smith_lint_changed`, runtime/verbs.c, and
`runtime/changed.c`, which `smith test --changed` shares): the files that differ from the merge-base of `HEAD` and `<ref>`
(default `origin/main`, else `main`; the working tree counts, so do untracked files) and every file that includes or
imports one of them, transitively, by the same graph as the tests'. A file the graph does not know is linted. These
paths select every file: a `.clang-tidy` anywhere, `build.c`, `build.smith`, a `Makefile` (they move what clang-tidy
says or the flags of a compile), and `shim/` and `patches/` (smith's own checks). The line `smith lint --changed: N
changed files since <sha>: M of K files to lint` says what was chosen, and no file affected is a success that says so
and `linted 0 files`. Plain `smith lint` still lints everything, as the merge train does. A path outside the project
folder is not seen (`git diff --relative`), and a file a source `#embed`s is not an edge of the graph. `smith build
preflight` runs `lint --changed`. Tests: `tests/lint_changed_test.smith`.

**`smith lint` builds what the files it lints wait for, not the program.** Plain `smith lint` builds `smith_needs`: the
generated files every compile reads (and, in smith's own build.c, the smith-cc link and the header of each of ~120 test
modules, which the smith under test must make). With `--changed` the build first runs only to write `.smith/build.info`
(the empty target `smith_info`), then builds the `need<TAB>source<TAB>file...` lines of the chosen sources, so editing a
`runtime/*.c` rebuilds nothing but the headers it includes (about 7 s for clang-tidy on a large file, from 50 s for the
relink and the 116 headers); a chosen source with no such line (a `.smith` module or a C file the build only declares with
`smith_compile_command`, unless build.c says what it reads with `smith_compile_needs`) builds `smith_needs`. Smith's own
build.c declares none yet, so its test modules and unbuilt sources do. A no-op `smith lint` after a build by the same smith builds nothing
(0.8 s); a build by another smith (a new compiler) rebuilds every object, which is that smith's doing, not the
executor's.

**The editor finds `.smith/compile_commands.json`:** `smith lsp` adds clangd's `--compile-commands-dir` for it when
the project has no root or `build/` one. **The editor has smith's defaults too** (`patches/smith-editor-defaults.patch`):
`smith lsp` sets clangd's default fallback style to `file:<smith's .clang-format>` (a fallback style may now be
written `file:<path>`, LLVM's style with that file on top) and gives its clang-tidy provider a fallback config
(`clang::clangd::FallbackTidyConfigFile`, used when no `.clang-tidy` governs a file). A project's file wins whole,
and clangd's own `--fallback-style` wins over smith's style; a stage0 runs Homebrew's clangd and has neither.

**`smith lsp` answers queries from the shell** (issue #248, `shim/lsp.cpp` "Queries from the shell"): `smith lsp def NAME`, `refs NAME`, `hover FILE:LINE:COL`, `symbols FILE`, `diag [FILE]` and `-` (a query a line on the standard input) are a client of the same clangd, on the same pipes and behind the same layer as the editor's (clangd runs in the process; no clangd header is needed, so no LLVM patch), so the answer comes from the editor's index on disk and hover and outline show the names as written. The client opens one project source so that clangd reads the compile commands and starts its background index, waits for the index's progress to end (`begin` and `end` of `backgroundIndexProgress`; nothing to index is not announced, so three seconds with no beginning is "nothing"), and a file changed since the index was written is indexed again by that run. A name is looked up in the index by the compiler's name (`geom_vec_dot` for `geom::vec::dot`, found by `geom_vec_dot`), and each answer in a `.smith` file is written as the lowering's table of that file says (the file is opened and parsed for it), so a bare name or the module path matches. `refs` asks `textDocument/references` at each definition, with the generated header's declaration (which C code reaches); a call of a generic, which the compiler sees as an instance, comes from the lowering's calls (`smith::CallSite`) of the project files that write the name. The output is grep's: `path:line:col: kind: text`, paths relative to the folder, status 1 when a `def`, `refs` or `hover` finds nothing or a `diag` is an error; a C macro is not in the index. Tests: `tests/lsp.sh` ("query").

**`.smith` is C to clang-format** (issue #25; `patches/clang-format-smith.patch`, about 100 lines): C's keywords
only, `::` one token, and `module ...;` and `import ...;` lines at file scope kept whole (`import <sys/wait.h>;`).
Inside a function `import` and `module` are names, as in C. clangd formats `.smith` the same way (same library).
- **One hook for a plugin's hover lines** (`patches/clangd-plugin-hover.patch`, LLVM r10, #263): `clang::clangd::registerHoverProvider(bool (*)(const NamedDecl &, HoverSection &))` is called for every hovered declaration and may add a section (a heading and lines of text); clangd knows nothing of what it says. The prototype uses only clang's AST and standard types, so a shim declares it without clangd's headers.
