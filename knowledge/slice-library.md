---
type: architecture
title: smith::slice and smith::ascii replace text's slice operations
description: A slice library generic over the element does what text did for chars, with no allocation, option for a search that may find nothing, and a character test that reads no locale.
tags: [libraries, slice, generics]
---
# smith::slice and smith::ascii

Owner (2026-10-24): "text is not anything special. It's just a slice." The operations a program does on a slice that are not an element access (compare, search, fill, reverse, cut) are one generic module, `smith::slice` (issue #199), and the tests of one character are `smith::ascii`. `text` is being replaced by them and by `format::parse` (part 2 of #199).

**Decisions.**
- *The element is in the declarator and generic.* `const auto xs[:]` (#193) takes a slice of any element and reads it; the second slice of `equal`, `compare`, `starts_with` and `ends_with` is written `typeof(a) b`, the first's type.
- *No allocation.* A result is an index, a bool, or the caller's slice written in place; no function takes an arena.
- *A search that may find nothing returns `option(usize)`.* No sentinel, no out parameter. In a generic's body the value is written `{.some = i}`.
- *`ascii` reads no locale* and is three functions of a `char`, so a predicate of a later `slice::trim` is one of them.
- *No C spelling.* Every export is generic; the header holds the record the compiler reads.

**What is built.** `equal`, `compare`, `starts_with`, `ends_with`, `find`, `find_last`, `find_if`, `find_slice`, `trim`, `count`, `fill`, `reverse`, `copy`, `take`, `take_until`, `take_while` (the cursors take `&s`, a pointer to a slice, #480). **What waits:** `search`, `sort`, `sum`, `min` and `max` wait for a concept in the element type (`const arithmetic xs[:]`); `reshape` is its own issue.
