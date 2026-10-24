---
type: design
title: Known gaps
description: A response file is opaque to .metal routing and Apple's metal output is not reproducible.
tags: [gaps]
---
# Known gaps

- A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.
- Apple's `metal` output is not reproducible (two identical builds of learner's shaders differ), so compare shaders by
  the command smith runs, not by bytes.
- `arena::push_slice(&a, T, n)` (#82) gives a `T[:]` of one dimension; more sizes (`push_slice(&a, f32, n, m)`) need a
  return type computed from the parameters (#102, #105), and `push_array` stays a `T *` until #105. The result of a
  generic call cannot be indexed or sliced where it is written (`push_slice(...)[1:3]`): bind it to a `T xs[:]` first. A
  module that imports arena is not in slice mode for that (the header carries no `smith-slices` line for a generic's
  head): it writes a `[:]` of its own to index.
- A slice in an aggregate initializer (`{.items = array}`) is C's brace elision (the pointer set, the count left 0), not a
  conversion; `-Wmissing-braces` reports it, so assign after declaring. `fields(T)`, `format` and `test::check_equal` over
  slices are not built (#82's comment of the lead; #83's `fields`).
