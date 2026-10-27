---
name: smith-slice
description: "Using smith::slice (import smith::slice; slice::equal, compare, starts_with, ends_with, find, find_last, find_if, find_slice, trim, count, fill, reverse, copy, take, take_until, take_while): functions over any slice, text included, with no allocation. Load when code imports smith::slice, on comparing, searching or filling a `T xs[:]`, or on replacing strncmp, memchr and a hand-written index loop."
---

# Functions over a slice: smith's slice

This skill ships in the smith plugin, from `.agents/skills/smith-slice/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Text is a `const char xs[:]`, bytes a `u8 xs[:]`, numbers an `i32 xs[:]`: one library does what C's `strcmp`, `memchr` and `memset` did for each (`smith-lang-arrays`). `smith::slice` (`lib/smith/slice.smith`, a module, no C spelling) is generic over the element, so each function is written `const auto xs[:]` (`smith-lang-generics`). Nothing allocates: a result is an index, a bool, or the caller's own slice written in place.

```smith
module parse;

import smith::slice;

export typedef option(usize) Index;

export Index separator(const char line[:]) {
    return slice::find(line, '=');           // an index, or .none: no sentinel
}

export bool is_port(const char line[:]) {
    const char prefix[:] = "port=";
    return slice::starts_with(line, prefix);
}

export usize commas(const char line[:]) {
    return slice::count(line, ',');
}
```

## The functions

- `equal(a, b)` and `compare(a, b)` (negative, zero or positive: the first difference, the shorter first); `starts_with(xs, prefix)` and `ends_with(xs, suffix)`.
- `find(xs, v)` and `find_last(xs, v)` return `option(usize)` (`smith-option`): `.none` when `v` is absent, no sentinel. `count(xs, v)` returns a `usize`.
- `find_if(xs, pred)` and `find_slice(xs, needle)` also return `option(usize)`; `pred` is a `constexpr auto` function of one element (`ascii::space`). `trim(xs, pred)` is a cut of `xs` without the elements at both ends for which `pred` is true; nothing is copied.
- `fill(xs, v)` sets every element; `reverse(xs)` reverses in place. Both write through a non-const slice. `copy(dst, src)` writes `src` over the start of `dst` (no overlap, `dst` has room).
- A cursor takes from the front of the caller's slice, given as `&s` (`smith-lang-slice-pointers`): `take(&s, n)`, `take_until(&s, v)` (skips the `v`, as `strsep` does; no `v` takes the rest) and `take_while(&s, pred)` return the cut and leave `s` after it. A line loop is `while (countof(s) > 0) { line = slice::take_until(&s, '\n'); ... }`.

## What trips

- **The first argument is a slice value**: a variable or `xs[a:b]`, not a string literal or a typedef'd slice (`typedef const char str[:]` fails as the `auto` argument); bind the literal first (`const char s[:] = "abc";`). The second argument of `equal`, `compare`, `starts_with` and `ends_with` has the first's type, so a literal needs a variable there too inside a generic call such as `test::check_equal`.
- **A caller names `option(usize)` at file scope first** (`typedef option(usize) Index;`), then reads it with `at == .some` and `at.some` (`smith-option`); `.some` as the first mention in a function that only compiles the syntax can fail to find its type, so bind and test the result as `lib/smith/slice_test.smith` does.
- Not built yet: `search`, `sort`, `sum`, `min`, `max` (they need `const arithmetic xs[:]`, a concept in the element type) and `reshape` (#199).

Also in this family: [smith-ascii](../smith-ascii/SKILL.md), [smith-format](../smith-format/SKILL.md) (`format::parse` reads a number).
