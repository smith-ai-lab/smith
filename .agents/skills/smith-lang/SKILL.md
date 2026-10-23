---
name: smith-lang
description: "The .smith language: C with modules, a superset of C by opt-in steps, what it needs, how it formats, lints, builds and shows errors. Load when a file ends in .smith, on `module`, `import` or `export` in C source, smith cc --smith-header or --smith-lower, or 'needs a smith whose clang has the token hook'."
---

# `.smith` files

A `.smith` file is C. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant, and compiles to the same code. A file opts into more, one step at a time; none is needed for the one before:

1. **Sized types** work in any `.smith` file: `u32`, `f64`, `usize` (load `smith-lang-sized-types`).
2. **Imports** work in any `.smith` file: `import lib::text;` for a smith module, `import <math.h>;` for a C header, whose names are then written `c::sqrtf` in a module or not (load `smith-lang-modules`, `smith-lang-c-interop`).
3. **A `module` line** at the top makes the file a module with exports and private names (load `smith-lang-modules`), and its parameters written as arrays keep their size (load `smith-lang-arrays`).
4. **Generic functions** in a module: parameters marked `constexpr type T` (called as `arena::push(&a, Point)`) or `auto x` (the argument's type, `typeof(x)` names it: `check_equal(auto a, typeof(a) b)`), and `if constexpr` in their bodies (load `smith-lang-generics`); `auto... rest`, lists (`auto args = {...}`) and `for constexpr` loops over them (load `smith-lang-lists`); `fields(T)`, a struct's fields as a compile-time list (load `smith-lang-reflection`).
5. **The target, and `if constexpr` in any function body**, in place of the preprocessor: `import target;`, then `target::arch == target::aarch64`, `target::address_sanitizer` (below).
6. **`main` with the io:** `i32 main(io::Io *io)` (with `import smith::io;`) is written as a `static` function and a C `main` that makes the io, calls it and closes the io, returning its status; the C forms (`i32 main(i32 argc, char **argv)` (in a file with no `module` line also `char *argv[]`), `int main(void)`) still compile as they do. `io::make` in a file with the io form is an error. Load `smith-io`.
7. **Tuples:** `struct { i32, f32 }`, a struct whose fields have no names, one type wherever it is written; `t[0]`, `countof(t)`, `return {a, b}` (load `smith-lang-tuples`).
8. **Functions at compile time:** `constexpr i32 x = fib(10);` and `static_assert(f() == 3)` run an ordinary C function in the compiler (load `smith-lang-constexpr`).

It is C2y, compiled with `smith cc x.smith`: the extension makes it `-x c -std=gnu2y` (`::` must lex as one token, and `auto` and `_Countof` work), with the other flags as for any C file. A `-std=` of yours wins. A small program is one command:

```
smith cc main.smith foo.smith -o prog   # main.smith: `module main; import foo; ...`, foo.smith: `module foo; export ...`
smith cc -c main.smith                  # compiles alone: foo.smith is found by the path its name implies
```

`import foo` with no `foo.h` on the include path reads `foo.smith` (among the command's `.smith` files, else beside the importing file under its module root: `a::b` is `a/b.smith`) and makes its interface in memory; nothing is written, and linking is as in C (every source on the command line). `smith build` writes the headers and uses them.

## What it needs

A smith whose clang has the token hook (patches/clang-token-hook.patch, applied by `smith build llvm`). A smith without it refuses a `.smith` input: `.smith files need a smith whose clang has the token hook`. `smith cc --smith-which x.smith` says `token-hook yes` or `no`. A stage0 (one `cc` command, no clang inside) has none, but can still write a module's header.

## Formatting

`smith fmt x.smith` formats a `.smith` file as C, keeping `::` paths and the `module`/`import` lines whole (load `smith-check`).

## Linting

`smith lint x.smith -- <flags>` lints a `.smith` file with the project's `.clang-tidy`, reading it as `smith cc` does and reporting at the source's lines with the names as written: a module's exports are judged by `readability-identifier-naming` as `area` and `Box`, not as `geometry_box_area`, and `--fix` edits the source's own tokens (load `smith-check`).

## Building them

`smith_object(b, &(SmithObject){.compiler = "smith-cc", .source = "src/game/replay.smith", ...})` in build.c is all a project does: `smith build` writes each module's header first (`.smith/include/a/b.h`, only when it changes), then compiles with `-I .smith/include`, reports an import cycle before any compile, and rebuilds only what an edit touches (a module's body: that module; its exports: its importers). See `smith-build` (".smith sources").

A project's build file can be `.smith` too: a root `build.smith` that is `module build;` with `import smith::build;` and `export void build(build::Build *b)` calls the build API by module names (`build::app`, `build::object`, `build::Build`; `smith build --dump-config` prints one). The full smith is needed to compile it. See `smith-build-smith` ("build.smith").

## Tests and proofs

`smith_tests` finds `.smith` tests: `void test_x(void) {` in a file with no `module` line, `export void test_x(void) {` in a module; the runner, `--test` and a failure's report name them as written (`geometry::box_test::area`, a `.smith` line and function). `smith prove geometry/box.smith` names functions as written too. See `smith-test` (".smith tests") and `smith-contract`.

## Two commands under the compiler

- `smith cc --smith-header src/a/b.smith -o .smith/include/a/b.h` writes a module's generated C header from its tokens alone (no compile, so all headers can be written first); `smith build` runs it. The header includes only what the exports use: `<stddef.h>`, `<stdint.h>`, the imported modules and `import <h>;` headers whose names (`x::name`, `c::name`) an export writes, never an `#include` or an import only a body uses (see `smith-lang-c-interop`).
- `smith cc --smith-lower x.smith` prints the C the file lowers to, with no macros and no parser (comments are dropped; `#include`, `#define`, `#pragma`, `#error` and the `#if` chains stay as written, and an `import` is written as the `#include` it stands for): how to see what a rule does.

## In the editor

`smith lsp` (clangd) lowers `.smith` files too, so an editor or an agent gets diagnostics (the lowering's own included), hover, go-to-definition across modules (and from a call to a generic to the generic), symbols and completion on them as on C. Positions are those of the `.smith` text; names in hover, symbols, completion, signature help and clang's messages are the names as written (`box::area` where the file imports the module, `area` in the module itself; `smith lsp` with `SMITH_LSP_RAW=1` shows the compiler's `geometry_box_area`), the same in `smith cc`'s diagnostics, and after `box::` completion offers the module's exports (`area`, inserted after the typed `box::`). The debugger shows the same names (`smith dbg` and `smith dap`): `geometry::box::area` in frames, `geometry::box::Box` as a type (see `smith-debug`). The file must be in `compile_commands.json` (`smith build` writes it) and the modules' headers built. See `smith-check` ("`.smith` files").

## Errors

Every error points at the `.smith` text. A C keyword used as a name (``i32 double(i32 x)``, `Box double` for a typedef of the module) is ``double` is a C keyword; it cannot name a function``, at the name, and nothing follows it (after a typedef from a C header, clang's own message). A module that is not found is one error that lists what was tried, and its uses report nothing more. `smith: ... is a C name; write c::...` and the module errors are in `smith-lang-modules`. If a `.smith` file fails in a way that looks like plain C, it is C: fix it as C.

Also in this family: [smith-lang-target](../smith-lang-target/SKILL.md).
