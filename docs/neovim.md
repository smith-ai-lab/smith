# Neovim

Neovim support for `.smith` files and for smith's C comes from this repository, with no fork of any Neovim plugin. It
needs Neovim 0.12 and `smith` on the `PATH`.

```lua
vim.pack.add({ "git@github.com:adam-r-kowalski/smith" })   -- HTTPS once the repository is public
vim.lsp.enable("smith")                                    -- optional: smith lsp for C, C++ and Objective-C
```

That is the whole setup. The plugin does nothing you did not ask for: it never enables a language server and never
changes a debug configuration of yours.

## What is in the repository

- `plugin/smith.lua`: the only Neovim file at the root (as fzf keeps `plugin/fzf.vim` at the root of a Go repository);
  it registers the `.smith` file type, puts `editors/neovim` on the runtimepath and loads `require("smith")`.
- `editors/neovim/`: the Lua code (`lua/smith/`) and the queries (`queries/smith/{highlights,folds,indents}.scm`, each
  starting with `; inherits: c`).
- `tree-sitter/`: the grammar (`grammar.js`) and the generated `src/parser.c`, committed, so that nobody needs the
  tree-sitter CLI or Node.

## What works now

- **`.smith` files** get the `smith` file type and tree-sitter highlighting: `module`, `import`, `as` and `export`,
  the `a::b` paths of modules (`module` for each name in a path, `punctuation.delimiter` for `::`), `c::printf` as a
  call, `tx::Text` as a type, `constexpr type T` parameters, and everything C highlights. Folds and indents are the queries (C's,
  inherited): `vim.wo.foldexpr = "v:lua.vim.treesitter.foldexpr()"` uses the folds; indents are for nvim-treesitter.
- **A `.h` file gets its language from the project**, not Neovim's guess (which is `cpp` for every `.h`). Only in a smith
  project (a folder at or above the file with `build.c`, `build.smith` or `.smith/`); elsewhere nothing changes. If you set
  `g:c_syntax_for_h` or `g:ch_syntax_for_h`, that is Neovim's rule and it wins. Otherwise a source beside the header with the
  same stem decides (`.c` or `.smith`: `c`; `.cpp`, `.cc`, `.cxx`: `cpp`; `.m`: `objc`; `.mm`: `objcpp`), then
  the `compile` lines of `.smith/build.info` (every source C or `.smith`: `c`; C and `.m`: `objc`), and with a C++ source
  among them, or no answer, Neovim's own detection. It costs a few `stat` calls and one small read per header opened.
  A `.h` mapping you add with `vim.filetype.add` after the plugin loads replaces this.
- **The parser** is compiled on first use (a few seconds, in the background, while the first `.smith` buffer opens) by
  `smith cc -shared` into `stdpath("data")/smith/parser/smith-<hash of parser.c>.so`, and again only when `parser.c`
  changes (`vim.pack.update`). Neovim 0.12 loads parsers itself, so nvim-treesitter is not required, and works if present.
  Without `smith` on the `PATH` the plugin says once that it cannot build the parser.
- **Plain C stays C**: `module`, `import`, `export`, `type`, `throw`, `new` and `class` are names wherever C allows names
  (`int import;`, `module = 1;`, `s->type`), and `u8` is a variable in a file that declares one.
- **`vim.lsp.enable("smith")`** runs `smith lsp` (clangd from smith's LLVM) for `c`, `cpp`, `objc`, `objcpp` and `smith`, with the
  root markers `build.c`, `build.smith`, `compile_commands.json` and `.git`. It replaces a `clangd` config: enable one.
  Do not add `--query-driver` (clangd then puts the SDK's include paths ahead of clang's own and the SDK's modules fail).
  Override anything with `vim.lsp.config("smith", { ... })`. A `.smith` file needs nothing more for a single file or a few
  modules in one folder: with no `build.c` and no `compile_commands.json` the server attaches in single-file mode and reads
  the file as `smith cc` does (`-x c -std=gnu2y`), finding `import foo` by `foo.smith` beside it. A real project wants
  the `compile_commands.json` that its `build.c` writes (`smith_compile_commands`), for its flags and include paths.
- **Debugging**: when nvim-dap is installed (on the runtimepath at startup, or when a C or `.smith` file opens), the plugin
  registers a `smith` adapter (`smith dap`) and two launch configurations for `c`, `cpp`, `objc`, `objcpp` and `smith`,
  each only when you have not set one. It registers; it starts nothing. The first, "smith: build and launch the program",
  is for a smith project (the folder with `build.c` or `build.smith`, found from the current buffer, else the working
  directory): its `program` runs `smith build` there, in nvim-dap's coroutine so the editor stays free, and on a failure
  shows the build's output (`vim.notify` at ERROR) and aborts the launch. Then it reads `.smith/build.info` and launches the
  one `program` that is among the `default` lines, as `smith run` picks it (and refuses with its message when there is none or
  more than one), by its absolute path, with `cwd` the project root. That is how a breakpoint in `main.smith` stops after
  `main.c` became `main.smith`: nvim-dap alone would launch whatever binary was last built. The second, "smith: launch a
  program", asks for a path, for a program the build does not name.
- **The plugin and the installed smith can be different commits.** At start the plugin compares its own commit
  (`git rev-parse HEAD` in its folder) with the one `smith version` prints (`... commit <hash>`, for a smith built
  from a checkout with `smith build`), and says once when they differ. `vim.g.smith_no_version_check = 1` silences it.

## What waits

- `smith dap` and `smith dbg` for `.smith` programs (names in backtraces, `b game::replay::open`): issue #27.
- Upstream registrations (Neovim's file types, nvim-treesitter, nvim-lspconfig), later, as contributions.

## The grammar

`tree-sitter-smith` extends tree-sitter-c (v0.24.2) as C++, CUDA and GLSL do, adding what `.smith` adds
([smith-lang.md](smith-lang.md), issue #11): `module a::b;`, `import a::b [as x];`, `import <h>;`, `export` (with `[[smith::c_name]]` attributes before it, on a
declaration, a function, a typedef or a tag, at file scope), `a::b` paths in expressions and types, `constexpr type T` parameters,
`if constexpr`, a type where an argument goes, and the sized types `u8` ... `usize`, `f16`, `f32`, `f64`. It also reads
what smith's own C needs and tree-sitter-c does not: `#embed`, `_Atomic(T)`, `_BitInt(N)`, `enum : unsigned char`, types
and operators as macro arguments (`SMITH_ARENA_PUSH(&a, long double)`, `SMITH_CHECK(a, ==, b)`), a macro that defines
functions without a semicolon, Apple blocks and `__block`.

One ambiguity is C's own: `module ;` and `module a;` are valid C when `module` is a type, and read as the module line.

Regenerating `parser.c` is a developer step: `scripts/tree-sitter.sh` (add `corpus` to run the CLI's own corpus tests). It
downloads the tree-sitter CLI 0.27.1 into `~/.cache/smith/tools` (sha256 of the release archive pinned in the script),
fetches tree-sitter-c with `smith fetch` (pinned by hash), and generates at ABI 15, the one Neovim 0.12 loads. Edit
`grammar.js` and the corpus in `tree-sitter/test/corpus`, regenerate, commit `src/`.

## Tests

`smith test` runs `tests/tree_sitter.sh` and `tests/neovim.sh`.

- `tests/tree_sitter.sh`: the corpus of `tree-sitter/test/corpus` (one test per construct, run by `tests/tree_sitter.c`
  without the CLI), the samples in `tests/tree_sitter_cases`, the #11 corpus (`tests/smith-lang/cases`, when it exists;
  `SMITH_LANG_CASES=<folder>` checks a copy), and every `.c` and `.h` file of smith, all of which must parse with no
  `ERROR` or `MISSING` node. The driver is compiled by smith against the tree-sitter runtime, pinned by hash.
- `tests/lsp.sh` (not Neovim): a real `smith lsp` session over stdio on a scratch project of two modules (`.smith` diagnostics
  at the right line and column, with the names as written, and a note at the declaration in the module; hover (at a
  definition too), references, highlights, rename, definition across modules, symbols and completion, `box::` included,
  all with the names as written; a C file unchanged; and with no
  `compile_commands.json`: one file, two modules found by their source, a hand-written database without `-x c`, and a
  command borrowed from a `.c` entry).
- `tests/neovim.sh`: headless Neovim, a process per test of `tests/neovim.lua`, in a scratch home (skipped with a message
  when `nvim` is absent): the file type, the compiled parser, the highlights, plain C, the LSP config and attaching to a C
  file and to a `.smith` buffer of a scratch project (with a clang error at its column) and of a folder with no
  project files at all, `.h` files (beside a `.c`, a `.cpp`, a `.m`, by the build's compiles, outside a project, with `g:c_syntax_for_h`), the DAP adapter and its two configurations (against a stand-in `dap` module: the build configuration is first; its `program`, run in a
  coroutine in a real `smith new` project, builds and returns `build/debug/<name>` while the event loop runs; a project whose build
  fails gets `dap.ABORT` and an ERROR notification; `smith run`'s rule for the program), a user's own adapter staying, and the
  version warning. `SMITH_NVIM_ONLY="parser highlights"` runs just those.
