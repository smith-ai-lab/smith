---
type: specification
title: Base64
description: std.base64, which writes bytes as text in the base64 alphabet and reads them back, in the standard form and in the form for an address.
status: built
built:
  - "`std.base64`, written in Smith, with `encode` and `encode_url`, which build their text with `Text` and `append_ascii`, and `decode` and `decode_url`, which build their list with `push`, and give `.none` for each text the page lists"
---

# Base64

The module `std.base64` writes bytes as text of 64 characters, three bytes to four characters, and reads such text back. It has two alphabets: the standard one, which ends in `+` and `/` and pads with `=`, and the one for an address and a token, which ends in `-` and `_` and pads with nothing.

```
fn encode(bytes: Slice[Byte]) -> str ! alloc
fn decode(text: str) -> Option[List[Byte]] ! alloc
fn encode_url(bytes: Slice[Byte]) -> str ! alloc
fn decode_url(text: str) -> Option[List[Byte]] ! alloc
```

```
use std.base64 (decode, encode, encode_url)

fn demo() ! alloc {
  a = encode("hi?".bytes)
  b = encode_url("hi?".bytes)
  c = decode("aGk/")
}
```

`a` is `"aGk/"` and `b` is `"aGk_"`. `c` is `.some` of the three bytes of `hi?`. `encode("hi".bytes)` is `"aGk="`, padded to four characters, and `encode_url("hi".bytes)` is `"aGk"`.

`decode` gives `.none` for a text that `encode` could not have written: a character outside the alphabet, a size that is not a multiple of four, padding anywhere but the end, or bits left over that are not zero. `decode_url` gives `.none` for a character outside its alphabet, any `=`, a size that leaves one character over, or bits left over that are not zero. So for every list of bytes, `decode(encode(bytes))` is `.some` of them, and for every text, `decode(text)` is `.some(bytes)` only when `encode(bytes)` is that text. The same holds of the other pair.

## The forms

| Written | Gives |
| --- | --- |
| `encode(bytes)`, `decode(text)` | The standard alphabet, padded with `=` |
| `encode_url(bytes)`, `decode_url(text)` | The alphabet for an address, with no padding |
