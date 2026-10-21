---
name: smith-lang
description: "The .smith language: C with modules, a superset of C by opt-in steps. Load when a file ends in .smith, when a build or message mentions .smith, `module`, `import` or `export` in C source, `smith cc --smith-header`, `--smith-lower`, the token hook, or 'needs a smith whose clang has the token hook'."
---

# `.smith` files

This skill ships in the smith plugin, from `.agents/skills/smith-lang/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The long reference is [docs/smith-lang.md](../../../docs/smith-lang.md).

A `.smith` file is C. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant, and compiles to the same code. A file opts into more, one step at a time; none is needed for the one before:

1. **Sized types** work in any `.smith` file: `u32`, `f64`, `usize` (load `smith-lang-sized-types`).
2. **Imports** work in any `.smith` file: `import lib::text;` for a smith module, `import <math.h>;` for a C header (load `smith-lang-modules`, `smith-lang-c-interop`).
3. **A `module` line** at the top makes the file a module with exports and private names (load `smith-lang-modules`).

It is C23 (`::` must lex as one token), compiled with `smith cc x.smith` (the extension makes it `-x c`) like any C file, with the same flags.

## What it needs

A smith whose clang has the token hook (patches/clang-token-hook.patch, applied by `smith build llvm`). A smith without it refuses a `.smith` input: `.smith files need a smith whose clang has the token hook`. `smith cc --smith-which x.smith` says `token-hook yes` or `no`. A stage0 (one `cc` command, no clang inside) has none, but can still write a module's header.

## Formatting

`smith fmt x.smith` formats a `.smith` file as C, keeping `::` paths and the `module`/`import` lines whole (load `smith-check`).

## Building them

`smith_object(b, &(SmithObject){.compiler = "smith-cc", .source = "src/game/replay.smith", ...})` in build.c is all a project does: `smith build` writes each module's header first (`.smith/include/a/b.h`, only when it changes), then compiles with `-I .smith/include`, reports an import cycle before any compile, and rebuilds only what an edit touches (a module's body: that module; its exports: its importers). See `smith-build` (".smith sources").

## Two commands under the compiler

- `smith cc --smith-header src/a/b.smith -o .smith/include/a/b.h` writes a module's generated C header from its tokens alone (no compile, so all headers can be written first); `smith build` runs it.
- `smith cc --smith-lower x.smith` prints the C the file lowers to, with no macros and no parser: how to see what a rule does.

## In the editor

`smith lsp` (clangd) lowers `.smith` files too, so an editor or an agent gets diagnostics (the lowering's own included), hover, go-to-definition across modules, symbols and completion on them as on C. Positions are those of the `.smith` text; names in hover, symbols, completion and clang's messages are the lowered C names (`geometry_box_area`), and completion after `x::` is empty. The file must be in `compile_commands.json` (`smith build` writes it) and the modules' headers built. See `smith-check` ("`.smith` files").

## Errors

Every error points at the `.smith` text. `smith: ... is a C name; write c::...` and the module errors are in `smith-lang-modules`. If a `.smith` file fails in a way that looks like plain C, it is C: fix it as C.
