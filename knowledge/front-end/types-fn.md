---
type: architecture
title: "Front end: front::types_fn"
description: "The front::types_fn part of smith's own front end: front::types_fn."
tags: [front-end]
---
# front::types_fn (`front/types_fn.smith`, #1515, C4)

Functions and lambdas in the checker; the rules are the top comment of the file. A lambda against an expected `fn(A) -> R` has that type, like one against `Fn(F)`.
- A parameter of type `fn_once(A) -> R` is linear (#1108): `check_once(c, params, body)` follows every path; types_decl calls it per function and types_expr per lambda.
- Reported: a second use ("used again"), a path that leaves before any use (`return`, end of body, `try`: "not used on every path"), a use inside a loop ("once per turn").
- A use is a call, or the name passed on or captured; an `fn(A) -> R` parameter may run any number of times.
- A stored lambda (`Fn(F)`) passes where `fn(A) -> R` is expected when the signatures are the same (#1649); `conform` in types_expr says so before the convert hook. A different signature is an error naming both; `fn_once` stays apart.
- A lambda written as an argument of a call is non-escaping and captures by reference (`note_arguments(args)` tells which are arguments).
- Any other lambda is stored (a variable, field or result): captures are copied by value, with no capture list (lead's decision, 2026-11-02).
- `check_lambda(c, e)` refuses a stored lambda that captures a local pointer or slice into the current function's stack (`p := &x`, `s := xs[1:3]`), naming the capture. A pointer parameter is allowed.
- Not yet: `fn_once` locals and fields, a lambda with no captures as a C function pointer, how a stored lambda's captures are freed (asked of the lead).
- No recursion: the walk is a work stack. Tests: `tests/front_types_fn_test.smith`.
