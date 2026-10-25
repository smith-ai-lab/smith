---
type: design
title: How the lowering works
description: The lowering is a state machine on the token hook, written against runtime/lang.h, with a table of the names it made and a small interface to clang.
tags: [lowering, internals]
---
# How the lowering works

- **The hook** is one patch to smith's clang (`patches/clang-token-hook.patch`, about 100 lines, applied by
  `scripts/llvm.sh`): `clang::TokenHook`, offered tokens as they leave `Preprocessor::Lex`, after macro expansion and
  before the parser. It may consume tokens and put others back with `EnterTokenStream`, or change the token where it is,
  and is told when the translation unit's `Sema` exists. Tokens replayed from the lookahead cache are not offered twice.
  A tool links a factory in; nothing else of clang changed. smith's factory (`shim/hook.cpp`) answers only for a main
  file called `*.smith`.
  - **It is offered little.** An identifier carries a bit (`IdentifierInfo::setHookWatched`); the hook is offered the
    watched identifiers and the end of the file, and, when it asks, the braces (a module's function bodies) or every
    token (a module's declarations). The Preprocessor keeps the last eight tokens for it (`getHookPrevious`), so a
    watched identifier still knows what came before it. Text of other files than the main one is never offered. Plain C
    in a `.smith` file is therefore not slower to compile than in a `.c` file, to within what can be measured
    (under 1%).
  - **What is watched:** `module`, `import`, `export`, the sized type names, each import binding (`c` included), and in a
    module its own file-scope names and the names its imported headers declared.
  - **The sized types are made by the hook**, with no macro in the way: at the first use of a sized name that nothing
    declares, the hook adds an implicit `TypedefDecl` to the translation unit (`Sema::PushOnScopeChains`), and the token
    stays the name as written, so clang's diagnostics say `i32` (aka `int`) and print no "expanded from macro" notes. To the
    lowering the name is still undeclared (the hook answers its questions that way), so a generated header gets the
    standard C types. Without a `Sema` (`clang -E`) the old macro text is used.
  - **After a sized type name** the hook looks at the next token whatever mode it is in (it is offered it, and passes over
    what it would not have been offered): a C keyword there that is no continuation of a type is reported by the lowering's
    own words (`smith_lang_keyword_name_use` reads the text around it for what it names) and read as an identifier.
    `smith cc --smith-header` does the same on its own tokens, so a bad module fails at its header.
  - **A module found by its source** is made in memory by `smith_lang_interface` (the header pass over a text; the
    lowering's tables are kept aside and put back, since an importer's own lowering is in the middle of its work). The
    text becomes the contents the file manager has for the source file, which the include then reads. The modules it
    imports are read first, the deepest first (`smith_raw_prefetch`, by the text of the `import` lines, at most eight
    modules, found among the files beside it as `--smith-header` finds them), because the interface borrows the tables of
    the lowering that asked for it and cannot ask for a second; so an export that takes `core::Thing *` is written with
    the `[[smith::c_name]]` that `core` chose (`CoreThing`), as the header `smith build` writes would, and
    `smith cc main.smith rolls.smith core.smith` needs no `-I .smith/include`. Unlike the header
    on disk it keeps the module's lines: each declaration is on its line (the first line is `#pragma once`) and its name
    at its column where the text before allows, so a diagnostic that points into it is at the declaration in the module,
    `foo.smith:3:12`. The `#include` lines it needs are entered by the hook in front of it, its imports of other modules
    found the same way are rewritten to include their sources, and `// smith-module:`, `// smith-imports:` and
    `// smith-names:` are what the lowering of an importer reads of it (`SmithInterface`, `runtime/lang.h`).
  - **The parser's tokens, for tools.** Output that stands for a token of the source (a renamed name, the head of an
    exported declaration) is pushed back as a plain stream and passes the hook again without being lowered a second
    time (`replayed`, "Replay" in `shim/hook.cpp`); what the source has no token of (a `static`, the `__asm__` label) is
    reinjected. A reinjected stream is not reported to the Preprocessor's token watcher, which is how clangd's
    `TokenBuffer` (behind hover, references, highlights, the outline) learns the parser's tokens, so without this none
    of an exported declaration's head, its name included, was known to them and hover at a definition said nothing.
    The prototype that carries an exported function's symbol is a copy of the head: its tokens are placed in a buffer of
    their own (`relocate`: a virtual file, since clangd places a declaration by its file entry and an exported function's first declaration is this copy), so that the declaration it makes is not a second one at the definition's place, and a
    diagnostic there is left out (the definition has its own).
  - **Diagnostics as written** go through a consumer in front of clang's (`Presenter`, `shim/hook.cpp`, in `smith cc` and
    in clangd alike): every message, note and fix-it is rewritten by the lowering's table of names, and a note into a
    module's generated header goes to the declaration in the module's source (`smith_lang_declarations` reads the
    module again for where it declares each export, so the header need not say). Uses of a module that could not be
    imported are kept out by the same consumer, by location.
- **The questions** the lowering asks the parser (`SmithHost` in `runtime/lang.h`) are answered from `Sema`'s name lookup
  in the current scope, so locals and parameters count and only completed declarations are visible (the hook runs one
  token ahead of the parser): is this name declared, and a type or a value; was it declared by an `import`ed header; and
  for a typedef or tag, its size, alignment and fields (names and the spelling of their types). The lowering uses the
  first two; the others are the questions first-class types and reflection will ask (issue #15), built and tested now
  (`SMITH_LANG_ASK=Packet,struct Header` prints what the parser answers at the end of the file). A type written as
  tokens (`const char *`, a typedef, `struct S`, the sized types' own names, pointers) is asked with `type_query`: its
  canonical spelling (typedefs looked through, the identity of a generic's instance), size, alignment, and whether it
  is an integer, a float, signed, a pointer, a bool, an enum or a record (`SMITH_LANG_ASK='?const char *,?Packet'`).
  `expr_type` and `value_query` run the parser's own `ParseTypeFromString` from inside the hook, that is inside the
  parser's `PP.Lex` of the token after one it has just consumed, and that leaves the parser's `PrevTokLocation` at a
  token of the question: a `}` read at that moment ended its function at the token asked about, and clangd's token
  buffer found nothing in the next function (hover, references, highlights: issue #133, a `[` of a slice struct's
  `count[1]` asked as a tuple subscript). So the lowering never asks about a token it made itself (`synthetic`).
  An expression is evaluated with `value_query` (issue #98): the tokens (a generic's `T` and a `for constexpr` variable
  already replaced) are read as `__typeof__(expr)` in the parser's scope, as `expr_type` does, and the expression is
  evaluated in a constant context, so a C function runs as in `constexpr` (the target's `sizeof(long)`, issue #97). The
  value is a tree (`SmithValue`: an integer with signedness and width, an `f32` or `f64`, a struct's fields, an array's
  elements; a string is `char`s) whose nodes the lowering provides, and a pointer, a union, an integer over 64 bits or a
  long double is an error. The lowering reads an integer from it where its own evaluator meets a name followed by
  `(`, `.`, `[` or `->` (`if constexpr (table_size(sizeof(T)) > 64)`, `i < config.count`); the function must be defined
  in the file the instance is written in (a generic's instance is compiled where it is called, where another module's
  function is a declaration, except an import's functions over `type`, which the importer defines, #107). The raw lowering has no `value_query` and says so. `SMITH_LANG_ASK='!grid.cells,!f(3)'`
  prints the tree.
- **The lowering** is C (`runtime/lang*.c`, included by smith.c): a state machine over tokens that knows nothing of
  clang. An exported declaration is read whole before it is written (its prefix and, for a function, a prototype with
  the symbol label are decided by all of it); the plain lexer (`runtime/lang_lex.c`, for `smith cc --smith-header`) reads every
  declaration that way. A private declaration of a module is decided as it streams by, which is what keeps the hook
  cheap: `static` goes before the first token (from what that token is, and `main` from a look at the file's text), the
  name after a type is the one declared, and an aggregate's body, an initializer and the declarators after an aggregate
  need no look ahead. `smith_lang_fast` (`runtime/lang_declare.c`) answers each token as keep, rename, sized type or
  drop, or says it needs the slow path (`smith_lang_feed`), which can look ahead; the answer is given before anything
  was changed. `SMITH_HOOK_STATS=1` prints how many tokens each way took.
- **In clangd**: `smith lsp` installs the same hook (`smith_clangd_main` calls `smith_hook_install`), so every parse of
  a `.smith` file in clangd (the open file, its preamble, the background index, a completion) is lowered as a compile is,
  and the source locations the hook keeps are the positions the editor shows. Two things differ from a compile.
  The lowering is C with static tables and no malloc, so one exists per process; a clangd parses many files at once, so a
  hook takes it when it is made and gives it back at the end of its file (`Lowering` in `shim/hook.cpp`), and
  `.smith` parses in a clangd run one at a time. And clangd does not know the extension, so every command it compiles a
  `.smith` file with gets the rule `smith cc` applies (`smith_lang_command`, `runtime/lang_command.c`, one function
  that both call): `-x c` before the file (before the `--` of clangd's commands), and `-std=gnu2y` unless the command names
  a standard. `smith lsp` applies it through `clang::CommandHook`, which `clang::createInvocation` (the one function that
  turns clangd's command into a compiler invocation: for the open file, the preamble and the background index) asks, so
  it holds for the fallback command of a folder with no `compile_commands.json`
  (`clang main.smith`), a hand-written database without `-x c`, and a command borrowed from a `.c` entry
  (`-x c-header`). The `compile_commands.json` that `smith build` writes carries `-x c -std=gnu2y` already, so a project
  is read exactly as it is built. With no database at all, a single file or a few modules in one folder just work:
  `import foo` is found by `foo.smith` beside the importing file, as for `smith cc main.smith foo.smith`, and the
  interface is made in memory (go-to-definition from `foo::f` lands in `foo.smith`). A real project wants its build.c's
  `compile_commands.json`, for its flags and include paths. The module headers are
  read through the file manager's file system, so a relative `-I .smith/include` means the command's directory. Because a
  module starts with code, clangd's preamble for it is empty, and the "translation unit with no declaration" extension
  warning that C gives for that is turned off for an empty main buffer. The messages go through `shim/lsp.cpp`:
  `smith_lsp_run` runs clangd's main with its input and output behind a pair of pipes, and a thread each way reads the
  protocol's messages, notes the requests (hover, completion, outline, signature help, hints, references, rename,
  workspace symbols) and rewrites the names in their answers with the table of the file asked about; a message that is
  none of those passes as the bytes it came in. The cursor of a completion that came right after `x::` is given to the
  lowering as a token (`SmithToken.completion`), which takes the `x::` away and leaves the cursor, so that the parser
  offers names there and the layer keeps the module's. A `c::name` that is no macro (`c::printf`, `c::FILE`) stays a
  plain name in the main file, at its place, in the parser's tokens that clangd's token buffer records (a name pushed
  back as a reinjection is not recorded, and a hover, a definition or a reference at it found no node, so
  go-to-definition fell back to the index by name and named every declaration of it, the SDK's and libc++'s `using`);
  a macro (`c::EOF`) is expanded from a stream of its own and clangd finds it as a macro. `tests/lsp_test.smith` drives a real
  session.
- **Routing**: `smith cc x.smith` runs clang as `-std=gnu2y -x c x.smith`; the extension turns the hook on. A command
  that says `-x c` itself keeps it and gets the standard (`-x c++` or another language is left alone). A smith whose clang
  has no hook (a stage0, or an LLVM built before the patch) refuses a `.smith` input with a message that says so.
- **A slice's struct is defined at file scope for an exported function** (issue #152): the prototype that carries the
  `__asm__` symbol and the definition after it each had `struct smith_array_T_n { ... }` in their parameter lists, two
  types; clang 23's `-fsanitize=function` (in `-fsanitize=undefined`, the debug profile) mangles a prototype-scope struct
  through its function and overflowed the stack on that pair (the same two C lines crash Homebrew's clang). The head
  writer (`smith_lang_hoist_slice_structs`, runtime/lang_head.c) writes each definition once before the head and
  registers the tag, so both prototype and definition name it; the generated header keeps the definition in the
  prototype. Corpus: `slice-export-sanitize`; the template's `primes::total` runs it in debug.
