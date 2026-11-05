---
name: smith-dev-syntax
description: "The tour of smith's new syntax (#1428): indentation blocks, `#` comments, `x := 5`/`mut n := 0`, `Point(x = 1, y = 2)`, `import std.math`, `x.to(i64)`, every feature with examples, one skill per area, and the map from today's C-like forms. Load before writing or reviewing anything in the new syntax: a front-end test, a ticket, an example, a skill, a fix-it."
---

# The new syntax, in one place

This family is the tour of the language the new front end (`front/`) implements. The spec is [#1428](https://github.com/smith-ai-lab/smith/issues/1428): its body is the source of truth, and these skills follow it. The `smith-lang-*` skills and the corpus still teach today's C-like syntax, which every file stays in until the switch-over (#1631). They are ported to this syntax with it.

Write tickets, tests of the new front end and examples in this syntax, never today's, and never the brace forms of earlier revisions (`{ }` blocks, `Point{x = 1}`, `T{}`, `@x{...}`). A form this tour does not show is not settled: ask the lead rather than invent one.

| Area | Skill |
|---|---|
| a whole program, for a first look | [smith-dev-syntax-program](../smith-dev-syntax-program/SKILL.md) |
| lines, comments, `do:`, declarations, `:=`, `mut`, `undefined`, pointers, conversions | [smith-dev-syntax-values](../smith-dev-syntax-values/SKILL.md) |
| the types: primitives, `?T`, `*void`, arrays, slices, blocks, tuples, `fn(...)`, `type`, `vec` | [smith-dev-syntax-types](../smith-dev-syntax-types/SKILL.md) |
| functions, defaults, named and keyword arguments, lambdas, trailing lambdas | [smith-dev-syntax-functions](../smith-dev-syntax-functions/SKILL.md) |
| method calls (UFCS) | [smith-dev-syntax-methods](../smith-dev-syntax-methods/SKILL.md) |
| structs, enums, payload enums, building values, named tuples, options | [smith-dev-syntax-data](../smith-dev-syntax-data/SKILL.md) |
| private fields, `init` functions, field defaults | [smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md) |
| `if`, `if x := opt:`, `match`, `while`, `while x := it.next():`, `defer`, `errdefer`, `try` | [smith-dev-syntax-control](../smith-dev-syntax-control/SKILL.md) |
| loops, iterators, generators, `for` expressions | [smith-dev-syntax-loops](../smith-dev-syntax-loops/SKILL.md) |
| types as arguments, inferred parameters, concepts, type functions | [smith-dev-syntax-generics](../smith-dev-syntax-generics/SKILL.md) |
| `const`, compile-time `if`, reflection, variadics | [smith-dev-syntax-compile-time](../smith-dev-syntax-compile-time/SKILL.md) |
| `import`, `from`, `pub`, C, C++ and Objective-C, globals, `extern`, build files, tests | [smith-dev-syntax-modules](../smith-dev-syntax-modules/SKILL.md) |
| attributes, `@when`, `@effects`, user attributes | [smith-dev-syntax-attributes](../smith-dev-syntax-attributes/SKILL.md) |
| today's form → the new form (what each fix-it writes) | [smith-dev-syntax-from-c](../smith-dev-syntax-from-c/SKILL.md) |

## The principles

- smith is its own language with excellent C, C++ and Objective-C interop, not a superset of C.
- Remove footguns, and never have two ways to write one thing: where two would parse, `smith fmt` or a fix-it rewrites one into the other.
- The parser is context-free: no symbol table, short fixed lookahead. `a.b` is always member access; the checker decides what `a` is.
- Everything the language provides is lowercase (`i32`, `str`, `num`, `type`, `to`, `@inline`); user and std types are capitalized (`Point`, `List`). Nothing from std is in scope without an `import`.
- Constant is the default, parameters included; `mut` marks one name that may change.
- Every declaration has a value (`= undefined` opts out). There are no zero values: every field is given, or the struct declares a default.
- Everything is an expression: a block's last line is its value; `return` leaves early.
- Blocks are `:` and indentation. Parentheses call, build a struct, group, wrap a payload or make a tuple; there are no braces.
- A type is a value: a caller passes one as an ordinary argument, `mem.alloc(u8, 10)`.

## Keeping the tour true

Every ` ```smith ` block in this family is parsed by the new front end in a test (#1802); a block that the checker supports is also checked. A change to #1428 changes these skills in the same PR as the code, or the lead's edit of the issue is followed by a PR here.
