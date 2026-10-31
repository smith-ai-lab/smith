---
type: design
title: A C header is read through clang's C++ libraries, at one edge file
description: shim/cimport.cpp parses one header with clang's C++ libraries and returns its functions and structs as one flat array of 32-bit words, which front::cimport reads into records; what it cannot express is listed with a reason.
tags: [front-end, c, llvm]
---
# A C header is read through clang's C++ libraries, at one edge file

The new front end (#1112) compiles `.smith` itself, so `import <stdio.h>;` must ask clang what a header declares (#1114, track C). It does so through clang's C++ libraries that smith already links (`clangTooling`, `clangFrontend`, `clangAST`), not libclang's C API, which the LLVM pack does not have: adding it would mean an LLVM rebuild.

- **The edge** is `shim/cimport.cpp`: `smith_cimport_read(request, words, capacity)` parses `#include <h>` or `"h"` with the caller's flags (`-I`, `-D`, `-isysroot`, `-resource-dir`) as C, with `-fsyntax-only`, no code generation and no modules, and walks the translation unit. Only declarations from the header and the headers it includes are read; builtins and implicit declarations are not.
- **The result** is one pointer-free array of 32-bit words in the caller's buffer (a string pool and tables of types, functions, structs, fields, parameters and not-imported entries; the layout is at the top of the file). A buffer that is too small gets the words needed, and the caller calls again. C++ containers stay inside the shim.
- **`front::cimport`** (`front/cimport.smith`) turns the words into records: functions, structs (`opaque` when undefined), a table of types referred to by index, and `error`. Its shape maps one to one onto `front::ast`'s declarations; the conversion is a ticket of its own. A function pointer is its own type kind, not a pointer.
- **Not imported** is a list with a name and a reason, never a guess and never silent: unions, bit-fields, `long double`, `__int128`, volatile, arrays without a size, functions without a prototype, inline functions with a body, and every attribute (nonnull, nothrow, Apple's availability). A declaration that uses such a type is still read, with an `unsupported` type that carries the C spelling, and is also listed. Enums are their integer type until C2.
- **A function's type is read as written** (`getTypeSourceInfo`): the redeclaration of a library builtin such as `strlen` otherwise carries the builtin's type and loses the typedef name `size_t`.
- **A missing header** is an error that names the header and the folders searched, not a crash.
- The test is `tests/front_cimport_test.smith` (a fixture header and the system's `<string.h>` and `<stdio.h>`, goldens in `tests/front/cimport/`), in the front end's runner.
