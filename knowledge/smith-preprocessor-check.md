---
type: contract
title: smith-preprocessor, the check that a .smith file has no directive
description: A clang-tidy check reports every directive, _Pragma and C-macro c:: name in a .smith file, each with its replacement.
tags: [lint, preprocessor]
---
# smith-preprocessor, the check that a .smith file has no directive

**`smith-preprocessor` is a clang-tidy check that a `.smith` file has no preprocessor** (`shim/preprocessor.h`,
registered in `shim/tidy.cpp`, so `smith lint` and the editor run it; on in smith's default `.clang-tidy` and smith's
own). It reports every directive (read from the file's text with the raw lexer), `_Pragma`, and every `c::` name that
is a C macro (looked up when the file ends, among the macros its imports defined: `c::memset` is one in a fortified
SDK), each with the replacement. `.c` files are untouched; `// NOLINT(smith-preprocessor)` opts a line out.
