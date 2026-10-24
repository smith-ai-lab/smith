---
name: smith-text-numbers
description: "Parsing and writing numbers with smith::text: u64, i64, hex, f32 and f64, exact round trips and what is refused. Load when converting text to numbers or numbers to text without libc."
---

# Numbers

```smith
import smith::text;

void parse(const char word[:]) {
    u64 u, h;
    i64 i;
    f32 f;
    f64 d;
    bool ok = text::parse_u64(word, &u);    // decimal digits
    ok = text::parse_i64(word, &i);         // an optional leading -
    ok = text::parse_hex(word, &h);         // lowercase hex digits, as text::write_hex writes them
    ok = text::parse_f32(word, &f);         // -90, 0.25, .5, 5., 1e-5, 2.5E+3
    ok = text::parse_f64(word, &d);         // the same, as a double
    (void)ok;
}
```

- **A failed parse writes nothing** and returns false: an empty word, a stray character (`+1`, `1 `, a capital hex digit), or a value that does not fit (`18446744073709551616`, `-9223372036854775809`, a float or double that would be infinity). Errors are values; there is no errno.
- Leading zeros of any count are fine, and `-0` is zero for the integers and negative zero for the float.
- **`text::parse_f32` and `parse_f64` are correctly rounded**: the result is the float or double nearest the decimal, ties to even, for any number of digits, as `strtof` and `strtod` give in the C locale, but independent of the locale and of the C library's version. A number too large for a float is refused; one too small rounds to zero or a subnormal. There is no `inf` or `nan`.
- The float path needs no heap: a decimal of 15 digits or fewer, with a power of ten up to 22, is one exact multiply or divide in `double` (unless the result is exactly halfway between two floats); anything else is divided exactly with a 1024-bit integer on the stack. Digits past the 128th only matter as "something nonzero follows".

## Write numbers

```smith
import smith::text;

usize write_line(char out[:], u64 tick, u64 hash) {   // out has room for the three numbers and two spaces
    usize length = text::write_u64(out, tick);       // each write needs countof(out) >= text::number_capacity (20), the longest: -9223372036854775808
    out[length++] = ' ';
    length += text::write_i64(out[length:countof(out)], -7);
    out[length++] = ' ';
    length += text::write_hex(out[length:countof(out)], hash); // always 16 digits, so hashes line up
    return length;
}
```

There is no terminator and no failure: the writes take a `char out[:]` and return the count written; `out` must have room for `text::number_capacity` characters (16 for hex; `contract::requires`, which `smith prove` makes the caller prove). `text::write_f64(out, d)` writes a finite double in the fewest digits (at most 17) that read back as the same bits (`0.1`, `1e22`, `-0`), in room for `text::f64_capacity` (32; a run-time check, not a contract, because the arithmetic is too costly to prove). `text::write_f32(out, f)` does the same for a float (at most 9 digits, `0.1f` is `0.1`, room for `text::f32_capacity`, 24); both stop on infinity and nan. A replay that must not depend on text records bits as hex or whole numbers.

Also in this family: [smith-text](../smith-text/SKILL.md), [smith-text-c](../smith-text-c/SKILL.md).
