---
type: specification
title: Text functions
description: The functions of std.text that cut a text in two or take a prefix off it, which give back pieces that share its memory.
status: planned
---

# Text functions

The module `std.text` holds the functions that give text back. A search or a comparison of text is a [list function](list-functions.md), since a [`str`](../language/text.md) is used as its bytes. What is here cuts a text, and gives each piece as a text.

```
fn split_once(s: str, at: str) -> Option[(str, str)]
fn strip_prefix(s: str, prefix: str) -> Option[str]
```

Each piece shares the memory of `s`, so nothing is copied and neither function carries `alloc`.

## Taking a prefix off

`strip_prefix` gives the text after the prefix, or `.none` when `s` does not begin with it.

```
use std.text (strip_prefix)

fn flag_name(arg: str) -> Option[str] { strip_prefix(arg, "--") }
```

`flag_name("--out")` is `.some("out")`, and `flag_name("out")` is `.none`. An empty prefix is one every text has, so `strip_prefix("out", "")` is `.some("out")`, and `strip_prefix("", "--")` is `.none`, as a prefix longer than the text is not at its front.

## Cutting at a part

`split_once` gives the text before the first `at` and the text after it, as a tuple. It gives `.none` when `s` does not hold `at`.

```
use std.text (split_once)

fn setting(line: str) -> Option[(str, str)] { split_once(line, "=") }
```

`setting("depth=3")` is `.some(("depth", "3"))`. `setting("a=b=c")` is `.some(("a", "b=c"))`, cut at the first `=` only, and `setting("depth")` is `.none`. Nothing is left out: `setting("=3")` is `.some(("", "3"))` and `setting("depth=")` is `.some(("depth", ""))`.

## The forms

| Written | Gives |
| --- | --- |
| `strip_prefix(s, prefix)` | `.some` of the text after `prefix`, or `.none` when `s` does not begin with it |
| `split_once(s, at)` | `.some` of the text before the first `at` and the text after it, or `.none` |
