---
name: smith-lsp
description: "The language server and editor intelligence: smith lsp (clangd from smith's LLVM) for C, C++, Objective-C and .smith, pointing an editor or Claude Code at it, .smith/compile_commands.json and compile_commands.json, go-to-definition, hover, diagnostics, completion, rename, 'No --query-driver', 'Error while reading shard', SMITH_LSP_RAW. Load when setting up or debugging the language server, when an editor shows wrong or missing diagnostics for a .smith file, or when asked how an agent gets code intelligence."
---

# smith lsp: the language server

This skill ships in the smith plugin, from `.agents/skills/smith-lsp/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there. Formatting and linting are in `smith-check`; Neovim's plugin in `smith-neovim`.

`smith lsp` is clangd, from the same LLVM as the compiler, with the arguments unchanged. It also reads `.smith` files: the lowering that `smith cc` does runs inside clangd's own parses, so positions are those of the text as written, and names are shown as written (`box::area` in an importing file, `area` in the module). A stage0 smith runs Homebrew's clangd (`$SMITH_CLANGD`) and cannot read `.smith`.

## Point a client at it

- **Command:** `smith lsp`, with **no `--query-driver`** (with it clangd cannot build the SDK's Clang modules and reports module errors).
- **Claude Code:** the smith plugin declares it for `.c .h .m .mm .cpp .cc .hpp .smith`, so an agent gets definitions, references, symbols and diagnostics after each edit once the plugin is enabled. `smith` must be on the PATH of the shell that starts Claude Code; disable the official `clangd-lsp` plugin if both are on.
- **VS Code:** `"clangd.path": "smith", "clangd.arguments": ["lsp"]`. **Helix:** `command = "smith"`, `args = ["lsp"]`. **Zed:** `"lsp": {"clangd": {"binary": {"path": "smith", "arguments": ["lsp"]}}}`. **Emacs eglot:** `("smith" "lsp")` for c-mode, c++-mode, objc-mode. **Neovim:** `vim.lsp.enable("smith")` with the smith plugin (`smith-neovim`), or `vim.lsp.config('clangd', { cmd = { 'smith', 'lsp' } })`.
- Start the server in the project folder.

## What it reads

- The compile database. A `compile_commands.json` at the root (or in `build/`, or one you name with `--compile-commands-dir`) wins. Otherwise, when the current folder or one above has `.smith/compile_commands.json` (every `smith build`, `run`, `test` or other verb writes it, with the flags of the build, `-x c -std=gnu2y` for `.smith` sources and `-I .smith/include`), `smith lsp` adds `--compile-commands-dir=<that folder>/.smith`. So **no root compile_commands.json is needed**: run `smith build` once. A new file is read with the command of a similar one until the next build. For a root file for another tool: `smith_compile_commands(b, "compile_commands.json")` last in build.c (or `-MJ` with a plain `smith cc`, `smith-cc`).
- No build file at all, or a single file or a few modules in one folder: every command for a `.smith` file gets `-x c -std=gnu2y` unless a standard is named, and `import foo` finds `foo.smith` beside the importing file, with go-to-definition from `foo::f` into `foo.smith`.
- In a project the modules' headers (`.smith/include`) must have been built once by `smith build`, or `import` reports that it cannot find the module.
- The project's `.clang-format` and `.clang-tidy` apply to the editor's formatting and clangd's tidy checks. smith's embedded defaults reach the editor only after `smith fmt --dump-config > .clang-format` and `smith lint --dump-config > .clang-tidy` (clangd takes no fallback file for either).

## What works on `.smith`

Diagnostics at the line and column of the source (the lowering's own, such as ``printf` is a C name; write `c::printf``, and clang's), hover, go-to-definition and references across modules (from a call of a generic to the generic), document symbols, completion (`box::` offers the module's exports), signature help, inlay hints. A "did you mean" note points at the declaration in the module's `.smith`. A run-time array is shown as written: `F32[:]`, `F32[:][2]`, `F32[:][:]`, `const char[:]`, `U8[restrict :]` and `U8[const :]` in a hover, the outline and a completion, never `struct smith_array_f32_n`. Not done: a rename of an exported name edits only what clangd's index has seen; hover in an importer shows types as the generated header writes them (`int32_t`, not `I32`); completion offers `#include` edits for C names; only one `.smith` file is lowered at a time, so a project of many modules opens a little slower than the same in C.

## Ask it from the shell

```sh
smith lsp def arena::push            # path:line:col: kind: name: the declaration, one line a name
smith lsp refs arena::push           # every use: aliases (x::push), the module path, calls of a generic, C names in C files
smith lsp hover src/a.smith:12:5     # what hover shows, one line each (the type of an `auto` local, as written)
smith lsp symbols src/a.smith        # the outline
smith lsp diag [FILE...]             # the editor's diagnostics, no build; status 1 on any error; no files: all sources
printf 'def random::Key\nrefs random::split\n' | smith lsp -    # a query a line, one index load
```

- A name is as written (`arena::push`, `box::area`, a bare `push` is every module's) or a C name (`smith_arena_push`). Output is grep's shape, paths relative to the folder, so it pipes into `grep`, `sort`, `head`.
- It is the editor's server and index: the first query in a project waits for the index (`smith lsp: bringing the index up to date` on stderr; minutes on a big project, then seconds), a file changed since is indexed again first. Use it instead of grep for a symbol in `.smith`; **a C macro is found with grep** (it is not in the index). `smith lsp diag FILE` after an edit made outside Edit or Write.

## Check it without an editor

```sh
smith lsp --check=main.smith        # parses the file and reports "All checks completed, N errors"
SMITH_LSP_RAW=1 smith lsp ...       # no rewriting layer: shows the compiler's names (geometry_box_area)
```

- `I[..] Indexing AST` lines are clangd's log on stderr. clangd keeps its index in `.cache/clangd`; `Error while reading shard` once after switching clangd versions is harmless (delete the folder to start clean).
- A diagnostic that names `geometry_box_area` instead of `box::area` means the layer is off (`SMITH_LSP_RAW`) or the file is not `.smith`.
- Wrong include paths or missing flags: read the file's entry in `.smith/compile_commands.json` (`smith build -t compdb` prints the compile commands).
- **Hover shows a function's effects** (a smith whose LLVM is r10 or later): `pure`, `effects: alloc, files`, with ` (declared)` when its attribute says so, for a function defined in the file you hover in (`smith-lang-effects`). A call into another file shows the callee's solved set from the project's summaries (`.smith/effects/`), whichever file was parsed first.
