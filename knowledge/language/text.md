---
type: specification
title: Text and bytes
description: A str as bytes known to be valid UTF-8, which are used as bytes with no call, made from bytes through utf8, and cut into pieces that share its memory.
status: partial
built:
  - "`s.size` and `s[i]` of a `str`, with `arith` proving the index as it does for a `List`"
  - "`loop b in s` over a `str`, which gives each of its bytes, once each, in order"
  - "a `str` passed where a `List[u8]` is a parameter's type, with no call and no copy, and where a generic function lends a `List[T]`, which makes `T` a `u8`, and is refused with one error when `T` is already another type"
  - "`utf8`, which takes a `Slice[u8]` or a text, copies nothing, and gives `.ok` of the text of the same bytes, or `.err` with the place where the first character that is not valid begins, the slice still the caller's; `Utf8Error` holds `at` as a `Nat`; a `List` is frozen first, as `utf8(freeze(out))`"
  - "`s.bytes` of a `str`, its `Slice[u8]`, with no call and no copy, read only like any slice"
  - "building a `str` by hand is refused"
---

# Text and bytes

A `str` is bytes that cannot change, kept together with a proof that they are valid UTF-8. It is an ordinary struct, of a [`Slice`](../library/slice.md) and a proof, in the way [A struct that holds a proof](proof-fields.md) describes.

```
struct str {
  bytes: Slice[Byte]
  valid: is_utf8(bytes)
}
```

So everything a `Slice` is, a text is: it has `Copy`, any number of names share it, and a piece of it shares its memory. And a `str` is used wherever bytes are read, with no call and no copy: its `size` is how many bytes it has, `s[i]` is a byte, and a function that takes a `List[u8]` to read is given a text as it is given a slice.

```
fn count_spaces(s: str) -> Nat {
  var n: Nat = 0
  loop i in 0..s.size {
    if s[i] == ' ' { n = clamp(n + 1) }
  }
  n
}
```

Bytes become text through `utf8`, which reads them once. It gives the text, which is the same bytes and no copy of them, or the place of the first byte that is not valid. It takes a `Slice`, so the bytes are the caller's still when they are not valid, and bytes a program built in a `List` are frozen first, as `utf8(freeze(out))`.

```
struct Utf8Error {
  at: Nat
}

fn utf8(bytes: Slice[Byte]) -> Result[str, Utf8Error]
```

The bytes of a text are its `bytes` field, a `Slice[u8]` that is read like any slice and cannot be assigned to. So a piece of a text is made a text again by `utf8(s.bytes[a..b])`, which copies nothing. The size of `s.bytes` is the size of `s` to `arith`, so a fact about one proves an index or a piece of the other, also through a name given `s.bytes`.

`utf8` of the bytes of `"hi"` is `.ok("hi")`. `utf8` of bytes whose first is 255, which no UTF-8 text holds, is `.err(Utf8Error(at: 0))`.

A piece of a text is a text. `s[a..b]` is the bytes from `a` up to `b` as a `str`, which shares the memory of `s`, so nothing is copied. Text implements [`Index`](indexing.md) for a range, and its `valid` asks two things: that the range lies inside the text, and that both of its ends fall between characters and not inside one.

```
fn word(source: str, start: Nat, end: Nat) -> Option[str] {
  match check(valid(source, start..end)) {
    .some(p) { .some(source[start..end]) }
    .none { .none }
  }
}
```

`arith` proves that a range lies inside a text. That an end falls between characters is a fact about the bytes, which a [`check`](check.md) supplies by looking at the byte at each end, however long the piece is.

- Nothing else makes a `str` from bytes, and no bytes are replaced to make them valid. So `utf8` is the only way, and a list with an invalid byte gives the `.err` above, never a text with the byte changed.
- A range of a text's bytes, as bytes, is taken from its bytes and not from the text.
- Text is not counted or indexed in characters. A character may be several bytes, so `"é".size` is 2, and a program that needs the characters reads the bytes.

## The forms

| Written | Is |
| --- | --- |
| `s.size` | How many bytes `s` has |
| `s[i]` | The byte at `i` |
| `s.bytes` | The `Slice[u8]` of the bytes of `s` |
| `utf8(bytes)` | `.ok(text)` of the same bytes, or `.err` with the place of the first byte that is not valid |
| `s[a..b]` | The bytes from `a` up to `b` as a `str`, sharing the memory of `s` |

The functions that cut a text and read a character are in [Text functions](../library/text.md) and [UTF-8](../library/utf8.md).

Next: [Ownership](ownership.md)
