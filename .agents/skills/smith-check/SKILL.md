---
name: smith-check
description: "smith check, smith fmt, smith lint and smith lsp: checking a project, formatting with .clang-format, clang-tidy with .clang-tidy, the editor and clangd. Load before committing, when a check fails, on 'code should be clang-formatted', clang-tidy findings, or when setting up an editor or the Claude Code language server for C."
---

# smith check, fmt, lint and lsp

This skill ships in the smith plugin, from `.agents/skills/smith-check/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`smith fmt`, `smith lint` and `smith lsp` are clang-format, clang-tidy and clangd, linked into smith from the same LLVM as the compiler, so every machine formats and lints alike.

## On the project: no arguments

- `smith fmt` runs the project's `fmt` task: it rewrites the sources to `.clang-format`.
- `smith lint` runs the `lint` task: clang-tidy with `.clang-tidy` over the sources, with the flags the build uses.
- `smith check` runs the `check` alias: everything that must pass before a change is done (format, lint, the docs, tests, and what the project adds; `smith new` writes a `docs` step that runs `smith docs`, see `smith-docs`). It takes no arguments but `-D` options.
- Before committing: `smith fmt`, then `smith check`. A project without these tasks gets `build.c defines no task or alias named check`; `smith new` writes them.
- `smith build check` and the other verb names are refused: use the verb.

## On files: with arguments

With arguments they are the tools, unchanged: `smith fmt -i main.c`, `smith fmt --dry-run --Werror main.c`, `smith lint --quiet main.c -- <flags>`, `smith lsp --check=main.c`. A project's `.clang-format` and `.clang-tidy` apply. A stage0 smith runs Homebrew's tools, or `$SMITH_CLANG_FORMAT`, `$SMITH_CLANG_TIDY`, `$SMITH_CLANGD`.

## `.smith` files

`smith fmt x.smith` (and `-i`, `--dry-run`, standard input with `--assume-filename=x.smith`) formats a `.smith` file as C, with its `::` paths, `module a::b;` and `import a::b as x;` lines and `import <sys/wait.h>;` kept whole; `smith fmt` with no arguments formats the project's `.smith` files too, when its build.c lists them (the one `smith new` writes globs `**/*.smith`). `import` and `module` inside a function are names, as in C. Needs a smith whose clang-format has the `.smith` patch (a stage0 runs Homebrew's, which reads `.smith` as C++ and so misreads C names that are C++ keywords, such as `throw`). clangd formats `.smith` files the same way.

## The editor

- Point clangd at the command `smith lsp`, with no `--query-driver` (with it clangd cannot build the SDK's modules). It reads the project's `compile_commands.json`, which `smith build` writes, whose commands name `smith-cc`.
- VS Code: `"clangd.path": "smith", "clangd.arguments": ["lsp"]`. Neovim: the plugin below, or `vim.lsp.config('clangd', { cmd = { 'smith', 'lsp' } })`. Helix: `command = "smith"`, `args = ["lsp"]`. Zed: `"lsp": {"clangd": {"binary": {"path": "smith", "arguments": ["lsp"]}}}`. Emacs eglot: `("smith" "lsp")` for c-mode, c++-mode, objc-mode.
- Neovim 0.12 with smith's plugin: `vim.pack.add({ "git@github.com:adam-r-kowalski/smith" })` (HTTPS once public), then `vim.lsp.enable("smith")`. The repository root holds `plugin/smith.lua`, which registers the `.smith` file type, compiles the tree-sitter parser on first use with `smith cc -shared` (highlighting of `module`, `import`, `export` and `a::b` paths, with C's own highlights, folds and indents inherited), defines the `smith` LSP config (`smith lsp` for c, cpp, objc and objcpp; root markers `build.c`, `build.smith`, `compile_commands.json` and `.git`), registers a `smith` nvim-dap adapter (`smith dap`) when nvim-dap is installed, and warns once when its commit differs from the one `smith version` prints. It enables nothing and replaces no setting of yours. `smith fmt` formats `.smith` files (below); the language server for them waits on issue #26. See docs/neovim.md.
- Claude Code: the smith plugin declares `smith lsp` for `.c`, `.h`, `.m`, `.mm`, `.cpp`, `.cc` and `.hpp`, so an agent gets definitions, references, symbols and diagnostics after each edit. `smith` must be on the PATH of the shell that starts Claude Code; disable the official `clangd-lsp` plugin if both are on.
- clangd keeps its index in `.cache/clangd`; "Error while reading shard" once after a Homebrew clangd is harmless (delete the folder to start clean).
