---
name: smith-neovim
description: "Neovim support from the smith repository: plugin/smith.lua, vim.pack.add, the .smith file type, the tree-sitter parser (from smith cc --smith-tree-sitter), vim.lsp.enable('smith'), the .h file type in a smith project, the nvim-dap 'smith: launch' configuration that debugs the test under the cursor, the neotest-smith adapter, and the plugin/smith version warning. Load when setting up Neovim for a smith project, on '.smith files have no highlighting', 'cannot build the parser', 'a .h file is cpp', nvim-dap or neotest with smith, or when asked how to debug or run a test from Neovim."
---

# Neovim

This skill ships in the smith plugin, from `.agents/skills/smith-neovim/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Reference: [neovim](../../../knowledge/neovim.md) in that repository. It needs Neovim 0.12 and `smith` on the PATH. The plugin enables nothing the user did not ask for and replaces none of their settings.

## Install

```lua
vim.pack.add({ "git@github.com:adam-r-kowalski/smith" }, { load = true })   -- HTTPS once the repository is public
vim.lsp.enable("smith")                                                      -- optional: the language server
```

`load = true` matters in `init.lua`, where `vim.pack.add` otherwise runs no `plugin/` file (and so neotest's setup below would not find the adapter). The repository's `plugin/smith.lua` is the only Neovim file at its root; the Lua code and queries are in `editors/neovim/`, the grammar in `tree-sitter/` (generated `parser.c` committed, so nobody needs the tree-sitter CLI).

## What you get

- **The `.smith` file type** and tree-sitter highlighting: `module`, `import`, `as`, `export`, `a::b` paths, `c::printf` calls, `constexpr type T`, plus everything C highlights; folds and indents inherit C's. The parser is the one the installed smith carries: the plugin runs `smith cc --smith-tree-sitter` in the background, which prints the path of the library (written once to smith's runtime folder), and loads it, so the grammar matches the smith. Without `smith` on the PATH, or with a seed-built smith, the plugin says once it cannot find it. nvim-treesitter is not required.
- **`.h` files get a language from the project**, not Neovim's `cpp` guess: only in a smith project (a folder above with `build.c`, `build.smith`, `main.smith`, `main.c` or `.smith/`). Your `g:c_syntax_for_h` wins; else a source beside the header with the same stem decides (`.c` or `.smith`: `c`, `.cpp`: `cpp`, `.m`: `objc`, `.mm`: `objcpp`), then the `compile` lines of `.smith/build.info`, then Neovim's own. A `vim.filetype.add` mapping of yours after the plugin loads replaces this.
- **The language server:** `vim.lsp.enable("smith")` runs `smith lsp` for `c cpp objc objcpp smith` with root markers `build.c`, `build.smith`, `main.smith`, `main.c`, `compile_commands.json`, `.git`. It replaces a `clangd` config: enable one of them. No `--query-driver`. Override with `vim.lsp.config("smith", {...})`. More in `smith-lsp`.
- **Debugging with nvim-dap** (when nvim-dap is installed): the plugin registers the `smith` adapter (`smith dap`) and one configuration, "smith: launch", for `c cpp objc objcpp smith`, only where you have set none, so nothing asks which to use. Start a session (`:lua require('dap').continue()`) and it decides, in a smith project, with `cwd` the project root:
  - **cursor inside a function named `test_...`:** it runs `smith build smith_needs`, builds the runner `.smith/build.info` names, and launches it with `--test <name> --in-process` (`a::b::test_x` when the buffer has `module a::b;`), so a breakpoint in that test stops. `--in-process` is needed because the runner forks workers and lldb stays in the parent.
  - **anywhere else:** it runs `smith build` and launches the one program `smith_default` names (as `smith run` picks it), by absolute path, so a breakpoint in `main.smith` stops even after `main.c` became `main.smith`.
  - a failed build shows its output and aborts; it asks for a path only without a project or when the build names no single program. Breakpoints are set as usual (`dap.toggle_breakpoint()`).
  - A hand-written nvim-dap configuration does not build first and launches a stale binary; the plugin's does.
- **neotest:** `require("neotest").setup({ adapters = { require("neotest-smith") } })` (after the plugin is loaded). A test file is a `.c` or `.smith` file named `test_*` or `*_test` in a smith project; tests are found with tree-sitter. Nearest test runs `smith test --test <name> --verbose`; a `.smith` module's file runs `--test <module>::`; a `.c` file or the suite runs `smith test --verbose`. A failure is shown at the failing check's line with the check as written and its values. The dap strategy debugs the nearest test as above (the runner built first, `args = { "--test", name, "--in-process" }`).
- **Version warning:** at start the plugin compares its commit with the one `smith version` prints and says once when they differ; `vim.g.smith_no_version_check = 1` silences it.

## Troubleshooting

- No highlighting on `.smith`: is `smith` on the PATH Neovim sees (start Neovim from the shell that has it), does `smith cc --smith-tree-sitter` print a path (a seed-built Linux smith says to rebuild with `smith build`), is the plugin loaded (`:scriptnames` lists `plugin/smith.lua`; see `load = true` above)?
- A `.smith` buffer has no diagnostics: `smith-lsp`.
- Formatting a `.smith` file: `smith fmt` (`smith-check`); the editor's own formatting uses the project's `.clang-format` only.
- Debugging outside Neovim (VS Code, Helix, Emacs, an agent): `smith-debug`, `smith-mcp`.
