---
type: architecture
title: "The new front end: front::types_iter"
description: "front::types_iter (front/types_iter.smith, #1514, C3): one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# front::types_iter (`front/types_iter.smith`, #1514, C3)

Iteration in the checker; the rules are the top comment of the file.
- `for x in xs` binds `x` to the element of an array, slice or block, or a byte of a `str`. A number, pointer, record or option is "not iterable" and the error names the fix (`range(n)`, `p[0:n]`).
- `range`, `refs`, `enumerate` and `zip` are functions returning streams, known by name when nothing in scope has that name (until the library has them).
- `range(a, b)` yields its integers; an unsuffixed literal argument takes the type of the non-literal ones (`range(0, n)` with `n: u32` yields `u32`; all literals give `i32`; mixed non-literals are an error, #1606).
- `refs(xs)` yields pointers; `zip` and `enumerate` yield tuples, so one name is refused and `for (i, x) in ...` is "not yet typed" (#1557).
- The loop's names live in the loop's scope and may not shadow. `zip` of unequal lengths is a checked failure at run time (the lead's recommendation, not yet the owner's word, #1514).
- `types_iter::built_in()` also takes `stream::Stream(T)` as iterable with element `T` (#1613); the `yield` rules are `types_stream` (#1545) and the emitter is #1546.
- Not yet: a `for` that builds a value, `continue` as a filter, `sum(for ...)`, user iterators and `utf8::chars`. Tests: `tests/front_types_iter_test.smith`.
