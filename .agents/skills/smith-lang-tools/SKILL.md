---
name: smith-lang-tools
description: "How .smith files go through smith's tools: --smith-lower and --smith-header (what the lowering writes), smith fmt and lint with names as written, smith build's headers and rebuilds, build.smith, tests and proofs named as written, and the editor and debugger. Load to see the C a file lowers to, or when a .smith file formats, lints, builds, tests or shows in the editor in a way you did not expect."
---

# `.smith` files in the toolchain

```sh
$ cat small.smith
module small;

export U32 triple(U32 x) { return x * 3; }
$ smith cc --smith-lower small.smith      # the C it lowers to: no macros, no parser, comments dropped
__UINT32_TYPE__ small_triple(__UINT32_TYPE__ x) __asm__("_small::triple");
__UINT32_TYPE__ small_triple(__UINT32_TYPE__ x) { return x * 3; }
$ smith cc --smith-header small.smith -o small.h     # the header C includes (below the comment lines):
uint32_t small_triple(uint32_t x) __asm__("_small::triple");
```

- `--smith-header` writes a module's generated header from its tokens alone (no compile, so every header can be written first); `smith build` runs it. It includes only what the exports use (`smith-lang-c-headers`).
- `--smith-lower` keeps `#include`, `#define`, `#pragma`, `#error` and `#if` chains as written, and an `import` as the `#include` it stands for. It is how to see what a rule does.

## Each tool

- **`smith fmt x.smith`** formats as C, keeping `::` paths and the `module`/`import` lines whole (`smith-check`).
- **`smith lint x.smith -- <flags>`** lints with the project's `.clang-tidy`, reading the file as `smith cc` does and reporting at the source's lines with the names as written: `readability-identifier-naming` judges `area` and `Box`, not `geometry_box_area`, and `--fix` edits the source's own tokens.
- **`smith build`**: `smith_object(b, &(SmithObject){.compiler = "smith-cc", .source = "src/game/replay.smith"})` is all a project does. It writes each module's header first (`.smith/include/a/b.h`, only when it changes), compiles with `-I .smith/include`, reports an import cycle before any compile, and rebuilds only what an edit touches (a body: that module; its exports: its importers). `smith-build` (".smith sources").
- **`build.smith`**: `module build;`, `import std::build;`, `export void build(build::Build *b)` calls `build::app`, `build::object`; `smith build --dump-config` prints one. It needs the full smith. `smith-build-smith`.
- **Tests and proofs**: `smith_tests` finds `void test_x(void)` in a file with no `module` line and `export void test_x(void)` in a module; the runner, `--test` and a failure's report name them as written (`geometry::box_test::area`). `smith prove geometry/box.smith` names functions as written. `smith-test`, `smith-contract`.
- **The editor and debugger**: `smith lsp` (clangd) lowers `.smith` too: diagnostics (the lowering's own included), hover, go-to-definition across modules and from a call to its generic, symbols, completion after `box::`. Positions are the `.smith` text's; names are as written (`box::area` in an importer, `area` in the module; `SMITH_LSP_RAW=1` shows `geometry_box_area`). `smith dbg` and `smith dap` show `geometry::box::area` and `geometry::box::Box`. The file must be in `compile_commands.json` (`smith build` writes it) and the headers built. `smith-lsp`, `smith-debug`.
