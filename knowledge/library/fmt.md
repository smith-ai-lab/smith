---
type: specification
title: Number bases
description: The functions of std.fmt that write a whole number in base 16 or base 2, padded with zeros to a least width.
status: planned
---

# Number bases

The module `std.fmt` holds `hex` and `binary`, which write a whole number as the text of its digits in another base. They are ordinary functions: a string's braces hold an expression and no format code, as [Strings that hold expressions](../language/interpolation.md) says, so a number in another base is written by calling one inside the braces.

```
fn hex(n: u64, width: u8 = 0, upper: bool = true) -> str ! alloc
fn binary(n: u64, width: u8 = 0) -> str ! alloc
```

Both allocate, as the text is new memory. `width` and `upper` have defaults, so a call names them, as [Functions](../language/functions.md) says of named arguments and defaults.

## Base 16

`hex` gives the digits of `n` in base 16, with no prefix, in upper case.

```
use std.fmt (hex)

fn demo() ! alloc {
  hex(255)
  hex(233, width: 4)
  hex(255, upper: false)
}
```

`hex(255)` is `"FF"`, `hex(233, width: 4)` is `"00E9"`, and `hex(255, upper: false)` is `"ff"`. The second is how a lexer error names a character, as in `"U+{hex(233, width: 4)}"`, which is `U+00E9`.

## Base 2

`binary` gives the digits of `n` in base 2, with no prefix.

```
use std.fmt (binary)

fn demo() ! alloc {
  binary(5)
  binary(5, width: 8)
}
```

`binary(5)` is `"101"`, and `binary(5, width: 8)` is `"00000101"`.

## Width

`width` is the least number of digits. Zeros are put before the digits until there are that many.

- A number with more digits than `width` is written in full, so `hex(255, width: 1)` is `"FF"` and `binary(5, width: 2)` is `"101"`.
- `width: 0`, which is the default, adds none.
- `hex(0)` and `binary(0)` are `"0"`, and `hex(0, width: 4)` is `"0000"`.

## The edges

The largest `u64`, 18446744073709551615, is `"FFFFFFFFFFFFFFFF"` in `hex`, sixteen digits, and in `binary` sixty-four ones. With `width: 20`, `hex` of it has four zeros before those digits.

## Narrower numbers

`n` is a `u64`. A number of a narrower unsigned type is given with `convert`, which [Conversion](../language/conversion.md) describes.

```
use std.fmt (hex)

fn byte(b: u8) -> str ! alloc { hex(convert(b), width: 2) }
```

`byte(10)` is `"0A"`.

## The forms

| Written | Gives |
| --- | --- |
| `hex(n)` | The digits of `n` in base 16, in upper case, with `alloc` |
| `hex(n, width: w)` | The same, with zeros before them until there are `w` digits |
| `hex(n, upper: false)` | The same, in lower case |
| `binary(n)` | The digits of `n` in base 2, with `alloc` |
| `binary(n, width: w)` | The same, with zeros before them until there are `w` digits |
