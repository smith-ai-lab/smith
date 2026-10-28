---
type: architecture
title: std::slice and std::ascii replace text's slice operations
description: A slice library generic over the element does what text did for chars, with no allocation, option for a search that may find nothing, and a character test that reads no locale.
tags: [libraries, slice, generics]
---
# std::slice and std::ascii

Owner (2026-10-24): "text is not anything special. It's just a slice." The operations a program does on a slice that are not an element access (compare, search, fill, reverse, cut) are one generic module, `std::slice` (issue #199), and the tests of one character are `std::ascii`. `text` is being replaced by them and by `format::parse` (part 2 of #199).

**Decisions.**
- *The element is in the declarator and generic.* `const auto xs[:]` (#193) takes a slice of any element and reads it; the second slice of `equal`, `compare`, `starts_with` and `ends_with` is written `typeof(a) b`, the first's type.
- *No allocation.* A result is an index, a bool, or the caller's slice written in place; no function takes an arena.
- *A search that may find nothing returns `Option(Usize)`.* No sentinel, no out parameter. In a generic's body the value is written `{.some = i}`.
- *`ascii` reads no locale* and is three functions of a `char`, so a predicate of a later `slice::trim` is one of them.
- *No C spelling.* Every export is generic; the header holds the record the compiler reads.

**What is built.** `equal`, `compare`, `starts_with`, `ends_with`, `find`, `find_last`, `find_if`, `find_slice`, `trim`, `count`, `fill`, `reverse`. **What waits on the language** (found building it): `copy` needs `auto dst[restrict :]` (a `restrict` inside the brackets of an `auto` parameter), `take` and `take_until` need `const auto (*xs)[:]` (a pointer to a slice of `auto` elements), and the second slice written `const typeof(a[0]) b[:]` (so a string literal converts there) is not read; a typedef'd slice (`typedef const char Str[:]`) as the `auto` argument fails at `typeof(xs[0])`. `search`, `sort`, `sum`, `min` and `max` wait for a concept in the element type (`const Num xs[:]` is "unknown type name" in the header); `reshape`, `format::parse` and deleting `text` remain (the C code of the protocol, mcp and json still uses `smith_text_*`, which a generic `format::parse` has no C spelling to replace).
