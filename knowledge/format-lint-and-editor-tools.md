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
nearest compile's command for it). A finding fails it, the last line is `smith lint: linted N files` (`smith check`:
`lint ok: N files`), and a project with `.smith` sources that lints none fails. A stage0, whose clang cannot read
`.smith`, leaves them out and says so.

**The editor finds `.smith/compile_commands.json`:** `smith lsp` adds clangd's `--compile-commands-dir` for it when
the project has no root or `build/` one. **The editor has smith's defaults too** (`patches/smith-editor-defaults.patch`):
`smith lsp` sets clangd's default fallback style to `file:<smith's .clang-format>` (a fallback style may now be
written `file:<path>`, LLVM's style with that file on top) and gives its clang-tidy provider a fallback config
(`clang::clangd::FallbackTidyConfigFile`, used when no `.clang-tidy` governs a file). A project's file wins whole,
and clangd's own `--fallback-style` wins over smith's style; a stage0 runs Homebrew's clangd and has neither.

**`smith lsp` answers queries from the shell** (issue #248, `runtime/lsp_session.smith` and `runtime/lsp_query.smith`): `smith lsp def NAME`, `refs NAME`, `hover FILE:LINE:COL`, `symbols FILE`, `diag [FILE]` and `-` (a query a line on the standard input) are a client of the same clangd, on the same pipes and behind the same layer as the editor's (clangd runs in the process; no clangd header is needed, so no LLVM patch), so the answer comes from the editor's index on disk and hover and outline show the names as written. The client opens one project source so that clangd reads the compile commands and starts its background index, waits for the index's progress to end (`begin` and `end` of `backgroundIndexProgress`; nothing to index is not announced, so three seconds with no beginning is "nothing"), and a file changed since the index was written is indexed again by that run. A name is looked up in the index by the compiler's name (`geom_vec_dot` for `geom::vec::dot`, found by `geom_vec_dot`), and each answer in a `.smith` file is written as the lowering's table of that file says (the file is opened and parsed for it), so a bare name or the module path matches. `refs` asks `textDocument/references` at each definition, with the generated header's declaration (which C code reaches); a call of a generic, which the compiler sees as an instance, comes from the lowering's calls (`smith::CallSite`) of the project files that write the name. The output is grep's: `path:line:col: kind: text`, paths relative to the folder, status 1 when a `def`, `refs` or `hover` finds nothing or a `diag` is an error; a C macro is not in the index. Tests: `tests/lsp.sh` ("query").

**The layer between the editor and clangd is `.smith`** (issue #540, `runtime/lsp*.smith`, linked with the hook): `lsp.smith` runs clangd's main with its input and output on a pair of pipes and a thread each way, `lsp_layer.smith` notes the requests and rewrites the answers, `lsp_names.smith` turns the compiler's names back into the source's, `lsp_editor.smith` tells the effects plugin what the editor does, `lsp_session.smith` and `lsp_query.smith` are the shell's queries, `lsp_frames.smith` reads and writes the messages, `lsp_json.smith` is the tree the layer edits (smith::json's parser and writer under it), `lsp_text.smith` the string toolbox and `lsp_world.smith` the calls of the world. What `.smith` can not do yet is C in `runtime/lsp_edge.c` and `shim/lsp_edge.cpp`, declared in `runtime/lsp_edge.h` with the issue that closes each gap: threads and locks (#556), pipes and descriptors (#557), the working folder, real path and modification time (#558), a child in a folder (#559), the table of written names, which `shim/names.h` keeps in C++ types (#560), and the files, console, clock and environment, which smith's own `.smith` can not call because the handlers runtime is not linked into smith (#564). A message that is deeper than 256 levels, a request id longer than 64 characters, more than 1024 requests waiting for an answer or more than 256 open documents is not followed (the answer passes as clangd wrote it).

**`.smith` is C to clang-format** (issue #25; `patches/clang-format-smith.patch`, about 100 lines): C's keywords
only, `::` one token, and `module ...;` and `import ...;` lines at file scope kept whole (`import <sys/wait.h>;`).
Inside a function `import` and `module` are names, as in C. clangd formats `.smith` the same way (same library).
- **One hook for a plugin's hover lines** (`patches/clangd-plugin-hover.patch`, LLVM r10, #263): `clang::clangd::registerHoverProvider(bool (*)(const NamedDecl &, HoverSection &))` is called for every hovered declaration and may add a section (a heading and lines of text); clangd knows nothing of what it says. The prototype uses only clang's AST and standard types, so a shim declares it without clangd's headers.
