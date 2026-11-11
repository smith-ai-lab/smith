---
type: architecture
title: "The new front end: front::types_attr"
description: "front::types_attr (front/types_attr.smith, #1628): one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# front::types_attr (`front/types_attr.smith`, #1628)

- An attribute may sit on its own line before a declaration, a struct field or an enumerator (`parse_decl`); one followed by a blank line, a comment or the end of the block is the error "an attribute marks the declaration on the next line". `@attribute` marks a struct as an attribute type.
- `types_attr::check_module` checks every `@name` written so: built-ins (`inline noinline cold pure affine c_name attribute effects`) pass, any other unqualified name must be an `@attribute` struct of the module and its arguments are a construction of it, known at compile time, and the arguments are matched as a construction matches them, positional first and then named (`types_expr::argument_field`, #1813): `@rename("x")` is `@rename(new_name = "x")`. `@x()` is a parser warning whose fix drops the `()` (`warn_empty_call`). `@mod::name` is `not yet typed` (one module is checked at a time). Tests: `tests/front_attr_test.smith`.
