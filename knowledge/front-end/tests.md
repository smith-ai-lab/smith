---
type: architecture
title: "The new front end: Tests"
description: "Tests: one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# Tests

The front end has its own runner (`tests/front_units/build.c`, `front-project`), because its tests link LLVM's C API ([build-tests](../build-tests.md)).
- Unit tests: `tests/front_*_test.smith`, one or more per module (`front_lex_test`, `front_parse_*_test`, `front_types_*_test`, `front_emit_*_test`, `front_fix_*_test`).
- Golden files under `tests/front/<area>/` hold dumps (ast, tokens, IR, checker types, parser cases); rewrite them with the runner's `--update` or `FRONT_UPDATE=1` for the corpus.
- The e2e corpus `tests/front/e2e/` is run by `tests/front_e2e_test.smith` through `compile::file`; each case is a module in smith's own syntax.
- To add an e2e case: add `<name>.smith` with `pub fn check() -> i32` returning 0, which is compiled, linked with a `main.c` and run; or add `<name>.error` beside it with the exact diagnostics (scratch folder written `<dir>`).
- The folder is in `.clang-format-ignore` (`smith fmt` would rewrite the new forms). No test file lists cases: the folder is read.
- Run all: `smith build test-front`; one test: `smith test --test <name>` (a glob such as `'front_types_expr_test::*golden*'` works for a group).
