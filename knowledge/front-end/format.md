---
type: architecture
title: "Front end: The formatter"
description: "The The formatter part of smith's own front end: The formatter."
tags: [front-end]
---
# The formatter (`front/format_space.smith`, `front/format.smith`, #1886)

`smith fmt` on smith's own front end, one style and no clang-format (the plan: #1886, with #1377's zero patches). Wired into `smith fmt` (#1890): a `.smith` file goes to `format::format` unless it is in today's syntax (a line starts with a form only today's syntax or C has, `parser::parse_file` reads it without an error, its first `parse_new` error has a fix, or it has a parse error and no line only the new syntax has) (C entries `front_format_accepts` and `front_format_run`, `runtime/front_flag.c`) (#2040); a file in today's syntax stays clang-format's until #1631. A new-syntax file with a parse error is reported and left as written, with exit 1. `.clang-format-ignore` is honoured.
- `format_space::space_between(prev, next, context)` is the space inside a line, a pure function of two token kinds (#1888).
- `format::format(source)` lexes with `tokenize_new`, adds the layout tokens and writes the file back: 4 spaces a level from `indent`/`dedent`, one statement a line, the space by `space_between`, `#` and `##` comments kept as written (taken from the gap between two tokens, trailing or on their own line, a comment before a dedent is written at the new level), at most one blank line in a row, none at the start or end, one final newline. A line that goes on inside a bracket or after a binary operator keeps its break and is indented two levels deeper than its statement. Lines over 100 columns are left as written. It re-indents any consistent indentation to 4. It is idempotent; an error of the lexer or the layout is its result, and `format_run` first runs `parse_new` and reports its first error as `path:line:column: message` without writing.
- Tests: `tests/front_format_test.smith` reads the pairs `tests/front/format/<name>.smith` and `.expected` and checks that formatting twice changes nothing over `tests/front/e2e/`.
