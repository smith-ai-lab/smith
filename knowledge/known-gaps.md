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
- `arena::push(&a, T, n)` giving a `T[:]` (#82) needs a generic whose return type depends on its number of arguments, which
  the generics cannot say yet (no `auto` return, no overload by arity); `push_array` returns a `T *` until that is decided.
- A slice in an aggregate initializer (`{.items = array}`) is C's brace elision (the pointer set, the count left 0), not a
  conversion; `-Wmissing-braces` reports it, so assign after declaring. `fields(T)`, `format` and `test::check_equal` over
  slices are not built (#82's comment of the lead; #83's `fields`).
