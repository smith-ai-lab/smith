---
type: specification
title: ASCII
description: The functions of std.ascii that say which kind of ASCII character a byte is, a digit, a hexadecimal digit, a letter, or a space.
status: built
---

# ASCII

The module `std.ascii` holds four functions that each take one byte and say whether it is a certain kind of ASCII character. A byte is a `u8`, which is what a [`str`](../language/text.md) holds at each place.

```
fn is_digit(b: u8) -> bool
fn is_hex_digit(b: u8) -> bool
fn is_letter(b: u8) -> bool
fn is_space(b: u8) -> bool
```

```
use std.ascii (is_digit)

fn digits(s: str) -> u64 {
  var n: u64 = 0
  loop i in 0..s.size {
    if is_digit(s[i]) { n = wrapping_add(n, 1) }
  }
  n
}
```

`digits("a1b22")` is `3`.

## The four kinds

| Function | True of |
| --- | --- |
| `is_digit` | `'0'` to `'9'` |
| `is_hex_digit` | `'0'` to `'9'`, `'a'` to `'f'`, and `'A'` to `'F'` |
| `is_letter` | `'a'` to `'z'` and `'A'` to `'Z'` |
| `is_space` | a space, a tab, a line feed, and a carriage return |

So `is_digit('7')`, `is_hex_digit('F')`, `is_letter('q')`, and `is_space('\n')` are all `true`, and `is_hex_digit('g')`, `is_letter('_')`, and `is_space('x')` are `false`.

## What they give for a byte that is not ASCII

A byte that is not ASCII, one of 128 or above, is none of the four. Each function gives `false` for it, so `is_letter(233)` is `false`, and so is every byte of `"é"`, which is not read as a letter.
