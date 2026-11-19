---
type: specification
title: Text functions
description: The functions of std.text that cut a text in two or take a prefix off it, which give back pieces that share its memory, and Text, a text that grows and becomes a str with nothing read again.
status: partial
built:
  - "`Text` of `std.text`, with `text`, `append`, `append_ascii`, and `finish`, whose field is private, so that no other module builds one, reads its bytes, or changes them; `finish` gives the `str` of its bytes with none read again"
  - "not `strip_prefix` or `split_once`"
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

An empty `at` is at the start of every text, as it is for [`find`](list-functions.md), so `split_once("depth", "")` is `.some(("", "depth"))`. So for every `s` and `at`, what `split_once` gives is `.none` or two pieces with `at` between them that make up `s`.

## Building a text

`Text` is a text that a program owns and adds to, as a [`List`](list.md) is to a [`Slice`](slice.md). It always holds valid UTF-8, as a `str` does, so when it is done it becomes a `str` with no byte read again.

```
struct Text {
  bytes: List[Byte]
}

fn text() -> Text
fn append(t: take Text, s: str) -> Text ! alloc
fn append_ascii(t: take Text, b: 0..128) -> Text ! alloc
fn finish(t: take Text) -> str
```

Its field is not `pub`, so nothing is put in it but through these functions, and each keeps it valid.

- `text()` is an empty `Text`, and asks for no memory.
- `append` adds the bytes of a text at the end, with one test for room and one copy. A text is valid, and two valid texts one after the other are valid.
- `append_ascii` adds one byte below 128. Every such byte is a whole character, so its range is all that is asked and nothing is tested when the program runs.
- `finish` takes the `Text` and gives its bytes as a `str`, which shares their memory. Nothing is copied or checked.

```
use std.console (console, print)
use std.text (append, append_ascii, finish, text)

fn pair(name: str, value: str) -> str ! alloc {
  text() |> append(name) |> append_ascii('=') |> append(value) |> finish()
}

fn main() ! console, alloc {
  print(pair("depth", "3"))
}
```

This prints `depth=3`.

Bytes that are not known to be valid become a text first, through [`utf8`](../language/text.md), and are then added: `append(t, try utf8(bytes))`. There is no function that adds bytes as they are.

```
use std.text (append_ascii, text)

fn wrong(b: Byte) -> Text ! alloc { append_ascii(text(), b) }
```

`wrong` is refused, because a `Byte` may be 128 or above, which is no character alone.

`std.list` has an `append` too, which joins two lists. A file that uses both writes one with its module, as [Importing a module](../language/qualified-names.md#a-name-that-two-modules-share) says.

## The forms

| Written | Gives |
| --- | --- |
| `strip_prefix(s, prefix)` | `.some` of the text after `prefix`, or `.none` when `s` does not begin with it |
| `text()` | An empty `Text` |
| `append(t, s)` | `t` with the text `s` at its end |
| `append_ascii(t, b)` | `t` with the byte `b`, which is below 128, at its end |
| `finish(t)` | The `str` of what `t` holds, with nothing read again |
| `split_once(s, at)` | `.some` of the text before the first `at` and the text after it, or `.none`; an empty `at` is at the start |
