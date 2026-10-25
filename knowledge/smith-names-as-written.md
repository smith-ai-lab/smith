---
type: design
title: Names as written in the editor, lint and diagnostics
description: smith lsp and smith lint lower .smith files too, and report names, notes and fix-its as the source wrote them, never guessed from underscores.
tags: [lsp, lint, names]
---
# Names as written in the editor, lint and diagnostics

**`smith lsp` lowers `.smith` files too.** `smith_clangd_main` installs the same token hook, so a parse of a `.smith`
file in clangd is lowered as a compile's is and positions are those of the source; no LLVM change. The lowering is
C with static tables, so a hook holds it from its making to the end of its file and clangd's `.smith` parses take
turns. clangd does not know the extension, so `shim/tool.h` sets `clang::CommandHook` (`patches/smith-command-hook.patch`,
asked by `createInvocation`) to the one rule of `smith cc` for `.smith` inputs (`smith_lang_command`,
`runtime/lang_command.c`): `-x c`, and `-std=gnu2y` unless a standard is named, for every command clangd uses, a
folder with no compile database included (a single file or a few modules need no build.c). Names are shown as written (`box::area`, `area` in
the module), never guessed from underscores: the lowering keeps a table of the names it made, a diagnostic consumer in
the hook rewrites messages, notes and fix-its (`smith cc` too) and sends a note into a generated header to the
declaration in the module's `.smith`, and a layer between the editor and clangd (`shim/lsp.cpp`, in process, pipes on
clangd's stdin and stdout; `SMITH_LSP_RAW=1` turns it off) rewrites hover, symbols, completion (`box::` offers the
module's exports), signature help and hints. A module's header has a `// smith-names:` line of its exports, which depends
on them only, so an edit to a body does not rebuild importers. A `c::name` that is no macro stays a plain token in the
main file (a reinjected one is not in clangd's token buffer, so hover and go-to-definition found nothing there), and
hover leaves out the `asm("_foo::f")` label of a module's name; the struct of a run-time array, `struct smith_array_f32_n`, is written `f32[:]` wherever the layer rewrites a name (the element is rewritten too, so `box::Box[:]`); an importer still reads a module's types as the header
writes them (`int32_t`), since a generated header says the standard C types (`tests/lsp_test.smith`, [the language](smith-language.md)).

**`smith lint` lints `.smith` files as written** (`shim/tidy.cpp`). clang-tidy builds each file's command with the
driver, not `createInvocation`, so `getStripPluginsAdjuster` (the last adjuster) asks the same `clang::CommandHook`
(`patches/smith-command-hook.patch`; same rule as `smith cc`), and the shim installs the token hook. A check reports from an engine of its own, so the hook puts the same consumer in front of it
(`smith::presentThrough`): names, notes and fix-its as written, a prototype's lowering copy moved to the definition's
name, a finding in a generated or in-memory interface dropped (the module's own file is linted as itself).
`readability-identifier-naming` judges the written name: a wrapper module, registered after clang-tidy's, replaces that
one check with `SmithNaming` (it holds the final real check, whose private decisions it reaches by an explicit
instantiation) which gives the declaration its member name from the lowering's table for the one question. Sound
because the table is the lowering's own, never underscores guessed; the rename is undone before anything else reads
it; a `.c` file's names are never in the table. Other checks see the AST as it is, so one that compares a name's
spelling still sees the lowered one. `smith lsp` registers the same module (clangd makes its built-in checks from the same registry), without `presentThrough` (its findings go through the lsp layer), so the editor judges names as written too (`tests/lint_smith_test.smith`, `tests/lsp.sh`, [the language](smith-language.md)).

`smith fmt` formats a `.smith` file as C (clang-format 23 with `patches/clang-format-smith.patch`, about 100 lines): C's keywords
only (so a C name such as `throw` is a name, not C++'s keyword), `::` one token, and `module ...;` and `import ...;` lines at
file scope kept whole, `import <sys/wait.h>;` included. Inside a function body, `import` and `module` are names, so
`import value = c.x + c.y;` is an indented declaration of a type named `import`. Every C file formats the same as `.c` and as
`.smith` (`tests/smith_fmt_test.smith`). `smith fmt` with no arguments formats the project's `.smith` files
(every file git sees). clangd, which uses the same library, formats `.smith` files the same way; understanding the
rest of a `.smith` file (hover, diagnostics, completion) is `smith lsp` (below).

The compiler sees `geometry_box_Box`; the source says `box::Box` where it imports the module and `Box` in the module
itself. Everything that shows a name to a person shows the written one: the compiler's diagnostics (`error: use of
undeclared identifier 'box::aera'; did you mean 'box::area'?`, `note: 'box::area' declared here` at
`geometry/box.smith:5:12`) and, in `smith lsp`, hover, the outline, signature help, hints and completion. The way back is
a table the lowering keeps of the names it made (`runtime/lang_names.c`, `smith_lang_written_find`), never a guess at
underscores (`a_b` can be a real C name): the module's own declarations, the exports of each imported module (from the
`// smith-names:` line of its header) and of the modules those import (written with their whole module name, as the
file has no name for them), and every `x::name` the file wrote, declared or not. `smith lsp` takes the table from the
hook when a parse ends (`shim/names.h`) and a layer between the editor and clangd (`shim/lsp.cpp`) rewrites the answers
(`SMITH_LSP_RAW=1` turns the layer off: the compiler's names). In an editor, after `box::` the module's exports are
offered, each as its member, and the edit replaces only what comes after the `::` (`box::` + `area`). The layer also
leaves out of references and renames the generated headers (`.smith/include/*.h`), which are no place to go to or edit.

The debugger shows them as well (issue #27): lldb's frames, `bt` and types say `geometry::box::area` and
`geometry::box::Box`, with the whole module name (the same in every object, so the types of two objects are one type),
from `patches/clang-debug-name.patch` and the table above; see knowledge/debugger.md for what lldb then accepts (a cast to
`geometry::box::Box` does; `b box::area` does not, `b geometry::box::area` does).

`smith lint x.smith` (clang-tidy 23 of smith's LLVM, in process) reads a `.smith` file as `smith cc` does and reports as
the source is written: the same hook, the same rule for the command, and the table of names above. `smith lint` with no
arguments lints what the project's `lint` task lists; what follows is what clang-tidy does with a `.smith` file it is
given (`tests/lint_smith_test.smith`).

- **The command.** clang-tidy builds the command of each file with clang's driver, not with `clang::createInvocation`, so
  `getStripPluginsAdjuster`, the last adjuster clang-tidy appends, asks `clang::CommandHook`
  (`patches/smith-command-hook.patch`), which `shim/tidy.cpp` sets to `smith_lang_command` (`-x c`, and `-std=gnu2y`
  unless a standard is named). A command with no `.smith` input is returned as it was (clang-tidy runs each file with a command of its own, so a run of `.c` and
  `.smith` files is right for each), and a compile database's `-x c -std=gnu2y` is already the rule.
- **The findings.** A check reports from an engine of its own, not the compiler's, so clang's diagnostics (the lowering's
  among them) went through the hook's consumer and a check's did not. When a check is created `shim/tidy.cpp` notes that
  engine, and the hook of a `.smith` file puts a consumer in front of it (`smith::presentThrough`): the names in a
  message, a note and a fix-it are the written ones, as for the compiler's. Two things differ for a tool. The first
  declaration of an exported function is the lowering's copy of its prototype, and a check reports a name once, at the
  first declaration it finds, so the finding is moved to the token the copy was made from (the definition's name) and
  a fix-it in a copy is dropped (the definition's own is what changes the source). And a finding in the interface of an
  imported module (the generated header, or the module's source read as one) is dropped: the user did not write that text
  there, and the module's own file is linted as itself. The lowering's own error uses a built-in diagnostic in a tool, so
  that its number is not mistaken for one of a check's.
- **The names.** `readability-identifier-naming` judges the name the source wrote. A module's file-scope names are
  `geometry_box_Box` to the AST, which is no `CamelCase`, so `shim/tidy.cpp` registers, after clang-tidy's own modules, one
  that wraps every check's factory and replaces this one's with `SmithNaming`, which holds the real check (it is `final`,
  and its decisions private) and asks it, for a name the lowering made, about the member name (`Box`, `area`):
  the declaration is given that name for the one question and the old one back. The table is the lowering's own for the
  file (never a guess at underscores), so `a_b` that is a real C name is judged as it is. The message and the fix-it
  (which replaces the token the source has) are about the written name. Locals, parameters, fields and every other check
  see the AST as it is, and a `.c` file, whose names are never in the table, is untouched. An importer's use of a
  module's badly named export is reported once, in the module's file; a fix there does not rename the importers.
- **Not done.** Checks that look at a name (a function, a variable) in a message are shown the written one, but a check
  that compares names in the lowered form (a length or a reserved-name check, say) still sees
  `geometry_box_x`; `readability-identifier-naming` is the one that has been made to see the written name.
- **`smith-preprocessor`** (`shim/preprocessor.h`, registered in the same module) reports, in a `.smith` file, every
  preprocessor directive (`#define`, `#undef`, `#include`, `#if` and its kin, `#pragma`, `#error`, `#warning`, `#line`,
  `#embed`) and `_Pragma`, and every `c::` name that is a C macro, each with what to write instead (`import`,
  `constexpr`, `if constexpr` with `target::`, `contract::assert`, `math::is_finite`, `math::max(usize)`; for another
  macro, that the module that wraps the C API should provide it). The directives are found by the raw lexer over the
  file's text (so a branch the compiler did not take counts, and `#error`, `#warning` and `#line`, which have no
  preprocessor callback, are not missed); the macros are looked up when the file ends, among those the file's imports
  defined: the lowering's own test, so `c::memset` and `c::snprintf` are macros in a fortified SDK. A `.c` file is left
  alone. A line that must keep one says `// NOLINT(smith-preprocessor)`; the corpus of `tests/smith-lang` and
  `tests/smith_lang_c_test.smith` compile `.smith` files with the preprocessor on purpose and are not linted (they are not among
  the build's compiles). `tests/lint_smith_test.smith` tests it.
- **`smith-module`** (issue #60; `shim/idiom.h`): a `.smith` file whose text has no `module` line (the raw lexer; comments are skipped) is reported at its first byte with the line its path implies, from the folder the check runs in (the project's root), `tests/` being a root of its own. The compiler never refuses such a file.
- **The idiom checks** (issue #47; `shim/idiom.h`, registered in the same module, on in the default `.clang-tidy`).
  `smith-sized-types` and `smith-module-library` read the `.smith` file's text with the raw lexer (a typedef's name is
  gone from the AST, and `unsigned` and `uint32_t` are one type): every C type spelling but `char` (`main` included: `i32 main(i32 argc, char **argv)`) with the sized type to write, and, for a header whose `// smith-module:` line says it is a
  module's, the `import <smith/arena.h>;` and every `c::` name of its `// smith-names:` pairs with the module's call.
  `smith-bounded-loop` and `smith-constexpr` match the AST (a loop whose condition is absent or a true constant outside
  `main`; a file-scope `const` of arithmetic, enum, or an array or record of them with a constant initializer, which a
  float, a pointer and a C object with external linkage are not), and `smith-c23` reads tokens (`NULL`, `_Bool`,
  `_Noreturn`, `<stdbool.h>`, `__attribute__((noreturn))`...) in every user file and the AST for `{0}`. The C checks
  skip C++. `tests/lint_smith_test.smith` has a passing and a failing file for each.
- **`smith-effects`** (`shim/effects.h`, registered in the same module) is a check of smith's own: a raw call into a
  system header is reported at the `c::` where the source wrote it, with the callee as written (`c::fopen`), a main-only
  constructor is named as the module writes it, and the rules about capabilities (a type marked
  `[[clang::annotate("smith::capability")]]`; the module that defines it may make raw calls) read the module's file as
  its implementation. The skill smith-io has the rules and how to fix each finding.
