---
name: smith-text
description: "Using <smith/text.h>, smith's reader and writer of small text formats: lines, words, and numbers (u64, i64, hex, f32) in memory. Load when code includes <smith/text.h>, when a build.c calls smith_library(b, \"text\"), or on a question about parsing or writing replays, scripts, configs or logs by hand in C without libc."
---

# Small text formats: smith's text

This skill ships in the smith plugin, from `.agents/skills/smith-text/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Text is a pointer and a length (`SmithText`), never a terminated string, so a file read into memory is read where it lies. The library calls no C library function and no locale, and every loop is bounded by a length. A number written here reads back as the same value on any machine. The header is `.smith/include/smith/text.h`; read it first.

## Read lines and words

```c
#include <smith/text.h>

SmithText text = {file.data, file.size};          // or smith_text_make("a terminated string")
SmithText line, word;
while (smith_text_take_line(&text, &line)) {      // a line without its newline, a trailing \r, or a # comment
    if (!smith_text_take_word(&line, &word)) continue;   // blank, or only a comment
    if (smith_text_is(word, "turn")) {
        float degrees;
        uint64_t ticks;
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_f32(word, &degrees)) { /* report the line */ }
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_u64(word, &ticks)) { /* report the line */ }
    }
}
```

- A word is a run without spaces or tabs. `smith_text_take_line` strips a `#` comment and the spaces before it, so `#` cannot appear in a word.
- `smith_text_is(word, "move")` compares a word to a terminated string, exactly.

## Parse numbers

```c
uint64_t u; int64_t i; uint64_t h; float f;
bool ok = smith_text_parse_u64(word, &u);    // decimal digits
ok = smith_text_parse_i64(word, &i);         // an optional leading -
ok = smith_text_parse_hex(word, &h);         // lowercase hex digits, as smith_text_write_hex writes them
ok = smith_text_parse_f32(word, &f);         // -90, 0.25, .5, 5., 1e-5, 2.5E+3
```

- **A failed parse writes nothing** and returns false: an empty word, a stray character (`+1`, `1 `, a capital hex digit), or a value that does not fit (`18446744073709551616`, `-9223372036854775809`, a float that would be infinity). Errors are values; there is no errno.
- Leading zeros of any count are fine, and `-0` is zero for the integers and negative zero for the float.
- **`smith_text_parse_f32` is correctly rounded**: the result is the float nearest the decimal, ties to even, for any number of digits, as `strtof` gives in the C locale, but independent of the locale and of the C library's version. A number too large for a float is refused; one too small rounds to zero or a subnormal. There is no `inf` or `nan`.
- The float path needs no heap: a decimal of 15 digits or fewer, with a power of ten up to 22, is one exact multiply or divide in `double` (unless the result is exactly halfway between two floats); anything else is divided exactly with a 1024-bit integer on the stack. Digits past the 128th only matter as "something nonzero follows".

## Write numbers

```c
char line[smith_text_number_capacity];       // 20, the longest: -9223372036854775808
size_t length = smith_text_write_u64(line, tick);
length += smith_text_write_i64(line + length, -7);
length += smith_text_write_hex(line + length, hash);   // always 16 digits, so hashes line up
```

There is no terminator and no failure: give room for `smith_text_number_capacity` characters (16 for hex). A float has no writer, because the right text for one depends on what the file is for: a replay records bits as hex or whole numbers, and `%.9g` from `snprintf` in a tool reads back exactly through `smith_text_parse_f32`.

## Replays must round-trip

Whatever the writers produce, the parsers read back exactly: every u64, i64 and hex value, including 0, the maximum, the minimum and leading zeros, and every finite float written with 9 significant digits (`%.9g`), signs of zero included. `tests/text.sh` checks it with property tests on keys, and `smith build text-exhaustive` parses all 2^32 float patterns, in two formats each, against `strtof` on all cores.

## Get the library in build.c

text needs nothing else. smith writes the header and the source into `.smith/` on every `smith build`:

```c
SmithObject text = {.compiler = "smith", .source = smith_library(b, "text"), .output = "build/smith/text.o", .flags = flags};
smith_object(b, &text);
```

`flags` includes `-I .smith/include`, and the program's link and `.objects` include it. Tests of code that uses it link it in `smith_tests`' `.objects`. See the smith-build and smith-test skills.
