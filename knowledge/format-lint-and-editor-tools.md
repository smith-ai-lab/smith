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

**`smith fmt` on many files is one cache and a few processes** (issue #639, `runtime/fmt_cache.c`): the cost was clang-format itself (about 2.7 ms a file, 5.2 s for smith's 1,950 files, run one batch of 128 after another; a process start is 13 ms), so a file whose content is known to be formatted is skipped and the rest go to processes of smith side by side, a few files each. The cache is `<cache>/smith/fmt/<identity of the smith>` (build-id on Linux, LC_UUID on macOS), a list of 16-byte keys: the SHA-256 of a file's content, its extension and every `.clang-format` from its folder up (smith's default when there is none). A key is added when the file passed `--dry-run --Werror` or was written by `-i`, so a changed file or a changed `.clang-format` is a new key and is never skipped; a check that fails runs the failed process's files one by one, so each is reported once and the others are cached. The project form prints `smith fmt: N files (M already formatted, K checked)`. File names with only `-i`, or `--dry-run --Werror`, take the same path; any other option is clang-format's own. `SMITH_FMT_CACHE=0` or `SMITH_CLANG_FORMAT` turns the cache off. Tests: `tests/fmt_cache_test.smith`.

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
