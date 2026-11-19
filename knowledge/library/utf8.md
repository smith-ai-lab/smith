---
type: specification
title: UTF-8
description: The functions of std.utf8 that read one character from bytes as its number and its length, and make the text of one character from its number.
status: planned
---

# UTF-8

The module `std.utf8` holds `decode` and `encode`, which turn between one character's number and its bytes, beside the `utf8` that makes a whole text from bytes, which [Text and bytes](../language/text.md) describes. A character's number is a `u32`.

```
fn decode(bytes: List[Byte], at: Nat) -> Option[(u32, Nat)]
fn encode(code: u32) -> Option[str] ! alloc
```

## Reading a character

`decode` gives the number of the character that begins at `at`, and how many bytes it takes.

```
use std.utf8 (decode)

fn demo() {
  decode("abc", 1)
  decode("é", 0)
  decode("a€", 1)
}
```

`decode("abc", 1)` is `.some((98, 1))`, the number of `b` and one byte. `decode("é", 0)` is `.some((233, 2))`, and `decode("a€", 1)` is `.some((8364, 3))`. A `str` is passed as the bytes, and so is any `List[u8]`, which need not be valid text.

`decode` gives `.none` in three cases:

- `at` is past the end, so `decode("abc", 3)` and `decode([], 0)` are `.none`;
- no valid character begins at `at`, as when the byte is 255, which no UTF-8 text holds, or the bytes end before the character does;
- `at` falls inside a character and not at its start, so `decode("é", 1)` is `.none`.

## Making the text of a character

`encode` gives the text of one character, and `.none` for a number that is no character. It allocates, as the text is new memory.

```
use std.utf8 (encode)

fn demo() ! alloc {
  encode(97)
  encode(233)
  encode(55296)
}
```

`encode(97)` is `.some("a")`, `encode(233)` is `.some("é")`, and `encode(55296)` is `.none`, as the numbers 55296 to 57343 are no characters. So is every number above 1114111. What `encode` gives is a `str` and not bytes, because it proves inside itself that the bytes are valid text, and a caller has nothing to check.

## The forms

| Written | Gives |
| --- | --- |
| `decode(bytes, at)` | `.some` of the number of the character that begins at `at` and the bytes it takes, or `.none` |
| `encode(code)` | `.some` of the text of the character numbered `code`, with `alloc`, or `.none` |
| `utf8(bytes)` | A whole text from bytes, as [Text and bytes](../language/text.md) says |
