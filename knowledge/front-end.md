---
type: architecture
title: smith's own front end for .smith files
description: A tokenizer, parser, checker and LLVM IR emitter for .smith in front/, reachable through smith cc --front-end=new (experimental, off by default), built in small tickets under #1112 and #1428.
tags: [front-end, parser, ast]
---
# smith's own front end

Plan: #1112 (the plan of record is its first comment), smith's own syntax and the wave tickets: #1428. Without a flag, a `.smith` file is lowered to C through a token hook in clang.
The new front end reads `.smith` itself and emits LLVM IR through LLVM's C API, with no patches on LLVM. It lives in `front/`.

`smith cc --front-end=new` (`front/compile.smith`, #1221, #1578) is the way in: experimental, off by default; without it a `.smith` file still goes through the token lowering.
`smith build`, `smith run` and `smith test` take the same flag (#2042): `--front-end=new` becomes `-DSMITH_FRONT_END=new` (`runtime/verbs.c`, `smith_build_main_front` in `smith.c`), `smith_object` adds `--front-end=new` to each `.smith` compile, so the flag is part of the command and of the cache key, and `smith cc` writes the `-MD -MF` depfile itself (`smith_front_depfile`). Any other value is a usage error. Test: `tests/build_front_end_test.smith`.
What they link (#2853): `smith cc --front-end=new` without `-c` links, like a C compile links libc, the embedded std.contract and every std module the program imports (directly, through other modules, or through a `.o` whose `<object>.std` list `-c` wrote), with `-lm`; the std sources are fixed once by `smith lint --fix` and compiled in `~/.cache/smith/front-std/<identity>/obj/<target>-<g|o>/` (`smith_front_std_prepare`, `runtime/front_flag.c`), and a library the command already names (`smith_<name>.o`, `-lsmith_<name>`, `lib/<name>.c`) is not added twice. `smith build` and `smith run` link through `smith cc --front-end=new` too, and the link waits only for the `.fx` summaries a compile step writes (the new front end writes none). Tests: `tests/hello_world_test.smith`, `tests/no_runtime_stubs_test.smith`.
The pipeline, one module group per section below:

- **lex, parse**: text to a `front::ast` tree (`lex`, `parse`, `parse_expr`, `parse_type`, `parse_stmt`, `parse_decl`, `parser`).
- **fix**: the fixes parse and check errors carry, applied by `smith lint --fix`.
- **check**: the tree to types (`types`, `types_expr`, `types_convert`, `types_decl`, `types_attr`, `types_fn`, `types_iter`, `types_stream`).
- **types**: the integer primitives are `i8 .. i64`, `i128`, `u8 .. u64`, `u128`, `isize`, `usize` (#2306): `i128` and `u128` are checker builtins (`I128`, `U128`), emit as LLVM `i128`, are C's `__int128`, and the fixer writes them for `_BitInt(128)` and `__int128`.
- **emit**: the checked tree to an LLVM module (`emit*.smith`), then an object (`emit_object`).
- **drive**: `compile` and `roots` join the stages for `smith cc`; `cimport` reads C headers ([c-header-import](c-header-import.md)).

Each file's top comment is the detail; this page is the map. The work shows through tests (see Tests at the end).
The old lowering (`runtime/lang*.c`, `shim/`) is never touched by front-end tickets.

## The pages

One page per area, so two front-end tickets edit two files (#3082: this page was the file most often shared by open pull requests, 159 of them from 11-03 to 11-11). A ticket names the one page it edits; a new area gets a new page and a line here.

- [front::ast (`front/ast.smith`, #1116)](front-end/ast.md)
- [front::lex and front::parse (`front/lex.smith`, `front/parse.smith`, #1128)](front-end/parse.md)
- [parse_expr and parse_type (`front/parse_expr.smith`, `front/parse_type.smith`, #1145)](front-end/parse-expr.md)
- [parse_stmt (`front/parse_stmt.smith`, A4)](front-end/parse-stmt.md)
- [parse_decl (`front/parse_decl.smith`, #1147)](front-end/parse-decl.md)
- [parser (`front/parser.smith`, #1219)](front-end/parser.md)
- [Fix-its and `smith lint --fix` (`front/fix.smith`, #1442, #1516)](front-end/fix.md)
- [The checker](front-end/check.md)
- [front::types (`front/types.smith`, #1220 J1a)](front-end/types.md)
- [front::eval (`front/eval.smith`, #3039)](front-end/eval.md)
- [front::types_expr (`front/types_expr.smith`, #1220 J1b)](front-end/types-expr.md)
- [front::types_convert (`front/types_convert.smith`, #1220 J1c)](front-end/types-convert.md)
- [front::types_decl (`front/types_decl.smith`, #1220 J1d)](front-end/types-decl.md)
- [front::types_fn (`front/types_fn.smith`, #1515, C4)](front-end/types-fn.md)
- [front::types_stream (`front/types_stream.smith`, #1545)](front-end/types-stream.md)
- [front::types_iter (`front/types_iter.smith`, #1514, C3)](front-end/types-iter.md)
- [Clang builtins (`front/types_builtin.smith`, `front/emit_builtin.smith`, #2286, part of #2224)](front-end/builtins.md)
- [front::emit (`front/emit*.smith`, #1119)](front-end/emit.md)
- [Objects and the driver](front-end/driver.md)
- [From disk (`runtime/import_cache.c`, `front/module_cache.smith`, `shim/cimport.cpp`, #2891)](front-end/disk-cache.md)
- [C import (`front/cimport.smith`, `front/cimport_cache.smith`)](front-end/cimport.md)
- [The language server (`front/lsp_server.smith`, #1887)](front-end/lsp.md)
- [The formatter (`front/format_space.smith`, `front/format.smith`, #1886)](front-end/format.md)
- [Tests](front-end/tests.md)
- [front::types_attr (`front/types_attr.smith`, #1628)](front-end/types-attr.md)
- [front::effects_table (`front/effects_table.smith`, #1965, piece 1 of #1939)](front-end/effects.md)
- [Return types and function names (`front/parse_type.smith`, `front/parse_decl.smith`, #1959)](front-end/return-types.md)
