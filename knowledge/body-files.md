---
type: design
title: An import's functions run at compile time from the module's body file
description: A module's own compile writes its lowered functions to a .body file beside its header, and an importer that mentions one where C requires a constant defines it, with what it needs, before its first declaration.
tags: [language, constexpr, modules, build]
---
# An import's functions run at compile time from the module's body file

**Compile-time power is ordinary functions** (issue #107; [functions at compile time](smith-language.md), skill
smith-lang-constexpr). Clang's evaluator runs a function only where it has the definition, and a module's header holds
a prototype and a label, so a call to `calc::fib` from another module was a call to a declaration. The header is made by
the plain lowering, which has no parser, so it cannot hold the body: slices, tuples, generic instances and the rest are
lowered with the parser's types. The module's own compile has the whole lowered text, so it writes it.

- **The file.** `SMITH_LANG_BODY=<path>` makes a compile write the lowered text of each declaration, split into
  functions (F exported, P private), types and constants (T), and state (V, a variable that is not constant), with the
  C includes of the module (H). It is C that may be included whole: each section is behind `#if
  defined(SMITH_BODY_<name>)`, and its first lines carry `__has_include` of the module's source, so an importer's
  depfile names the file and the source. An exported function is written `[[gnu::gnu_inline]] extern inline`: the copy
  is evaluated (and inlined) and emits no code, so the module's one symbol stays the function (a weak copy would be an
  object-code copy in every importer). The header says which exported functions have a body (`// smith-bodies: a b`);
  a module with none has no file and costs nothing.
- **The text is what the compiler read.** A sized name the lowering made from a type's spelling (the `u8` of a slice's struct
  in a body) is written as the compiler's name for the type (`__UINT8_TYPE__`), because the buffer an importer includes
  is read as written, with no hook to read `u8` as the type (issue #881).
- **Who writes it, and when.** Nothing in the build graph: an importer asks for the file when it needs it. The build
  cannot know before the headers exist what an importer mentions, and a step for each module would double the front
  end of every module whether it is needed or not. The importer's compile (`shim/hook.cpp`, `readBody`) takes
  `<header>.body` if it is newer than the module's source, else runs `smith cc -fsyntax-only` of that source with the
  importer's own `-I`, `-D` and `-std` (`SMITH_LANG_FLAGS`, set by the driver) and `SMITH_LANG_BODY`, and renames the
  result in place. A module found by its source has the file made in memory (a temporary file that is read and
  removed). The editor reads a file a build wrote and never starts a compile.
- **When it asks.** After the last `import` line, for each import with a `// smith-bodies:` line, for each listed
  function the file *mentions where C requires a constant*: a statement with `static_assert`, `constexpr`, `case` or
  `enum` (so an `if constexpr` condition), an initializer or an array bound outside a function body, and a function
  head that has a computed result type. A run-time call asks for nothing (a file of a hundred calls costs what it did).
- **What is defined.** The mentioned functions and, by the names their text uses, the module's other functions, types
  and constants, and the exports of the modules it imports (their headers are included, and those with a body are
  taken in turn: an import of an import works without the file importing it). The copies come in an included buffer
  that is a system file, with `-Weverything` off in it: no check or warning of the importing file reads them, and the
  effects check ignores its redeclarations.
- **The depfile.** Everything included from a system buffer is a system header, which `-MMD` leaves out, so the
  assembled text names each file it reads with a `#if __has_include(...)` (a lookup is recorded by where it finds the
  file, not by who asks): each taken module's `.body`, its source (a body made in memory has no file, so its source
  line is copied from the body's text) and the C headers of its `H` section. An edit to any of them rebuilds the
  importer. The compile that first makes a body file starts before it, so ninja's rule runs that compile once more. State is the one limit by design: a function that names a variable of the
  module that is not constant is not carried (a copy would be a second variable), and a call to it is the error it was.
  A function whose evaluation calls a generic's instance is not carried yet (the instance is written at the end of the
  module's file).
- **Cost.** The header gains one line per module with exported bodies (`include/smith/*.h`: about 60 bytes each); the
  body file is about the size of the module's lowered text, written once and only when an importer asks. Building smith,
  game and learner starts a compile of a module for its body only where a statement of an importer asks.
