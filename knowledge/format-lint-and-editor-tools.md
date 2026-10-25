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
