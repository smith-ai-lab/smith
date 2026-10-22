---
name: smith-text
description: "Using <smith/text.h>, smith's reader and writer of small text formats: lines, words, and numbers (u64, i64, hex, f32, f64) in memory. Load when code includes <smith/text.h>, when a build.c calls smith_library(b, \"text\"), or on a question about parsing or writing replays, scripts, configs or logs by hand in C without libc."
---

# Small text formats: smith's text

This skill ships in the smith plugin, from `.agents/skills/smith-text/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Text is a pointer and a length (`SmithText`), never a terminated string, so a file read into memory is read where it lies. The library calls no C library function and no locale, and every loop is bounded by a length. A number written here reads back as the same value on any machine. The header is `.smith/include/smith/text.h`; read it first.

**text is a `.smith` module** (`module smith::text;`, `lib/smith/text.smith` in smith's repository, issue #39), and its C header is generated from it. From `.smith` it is `import smith::text;` and the names are the module's: `text::Text`, `text::make(...)`, `text::take_line(&t, &line)`, `text::parse_u64(word, &x)`, `text::number_capacity`. C keeps its API under "From C". Compile the module with a smith that has clang inside (the `.smith` source is `.smith/lib/smith/text.smith`; a stage0 cannot compile it, though C code that only includes the header needs nothing). `smith cc main.smith lib/smith/text.smith -I include` builds a single command.

## Read lines and words

```smith
import smith::text;

// A replay: "turn -90 5" lines. Returns false at the first bad line.
bool read_replay(text::Text input) {
    text::Text line, word;
    while (text::take_line(&input, &line)) {         // a line without its newline, a trailing \r, or a # comment
        if (!text::take_word(&line, &word)) continue; // blank, or only a comment
        if (text::is(word, "turn")) {
            f32 degrees;
            u64 ticks;
            if (!text::take_word(&line, &word) || !text::parse_f32(word, &degrees)) return false;
            if (!text::take_word(&line, &word) || !text::parse_u64(word, &ticks)) return false;
        }
    }
    return true;
}
```

A file read into memory is `text::Text text = {data, size};` or `text::make("a terminated string")`.

- A word is a run without spaces or tabs. `text::take_line` strips a `#` comment and the spaces before it, so `#` cannot appear in a word.
- `text::is(word, "move")` compares a word to a terminated string, exactly.

## Parse numbers

```smith
import smith::text;

void parse(text::Text word) {
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

usize write_line(char *out, u64 tick, u64 hash) {
    usize length = text::write_u64(out, tick);   // out has room for text::number_capacity (20), the longest: -9223372036854775808
    out[length++] = ' ';
    length += text::write_i64(out + length, -7);
    out[length++] = ' ';
    length += text::write_hex(out + length, hash); // always 16 digits, so hashes line up
    return length;
}
```

There is no terminator and no failure: give room for `text::number_capacity` characters (16 for hex). `text::write_f64(out, d)` writes a finite double in the fewest digits (at most 17) that read back as the same bits (`0.1`, `1e22`, `-0`), in room for `text::f64_capacity` (32). A float has no writer, because the right text for one depends on what the file is for: a replay records bits as hex or whole numbers, and `%.9g` from `snprintf` in a tool reads back exactly through `text::parse_f32`.

## Replays must round-trip

Whatever the writers produce, the parsers read back exactly: every u64, i64 and hex value, including 0, the maximum, the minimum and leading zeros, and every finite float written with 9 significant digits (`%.9g`), signs of zero included. `tests/text.sh` checks it with property tests on keys, and `smith build text-exhaustive` parses all 2^32 float patterns, in two formats each, against `strtof`, and 2^24 sampled double patterns against `strtod`, on all cores.

## From C

C code is unchanged by the port: `#include <smith/text.h>`, the type `SmithText`, and `smith_text_<name>` for every function (`smith_text_make`, `smith_text_take_line`, `smith_text_parse_f32`, `smith_text_write_u64`...), with the capacities `smith_text_number_capacity` and `smith_text_f64_capacity`. The same loop:

```c
#include <smith/text.h>

SmithText text = {file.data, file.size};          // or smith_text_make("a terminated string")
SmithText line, word;
while (smith_text_take_line(&text, &line)) {
    if (!smith_text_take_word(&line, &word)) continue;
    if (smith_text_is(word, "turn")) {
        float degrees;
        uint64_t ticks;
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_f32(word, &degrees)) { /* report the line */ }
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_u64(word, &ticks)) { /* report the line */ }
    }
}
char out[smith_text_number_capacity];
size_t length = smith_text_write_u64(out, tick);
```

## Get the library in build.c

text needs nothing else. smith writes the header and the source (`.smith/lib/smith/text.smith`) into `.smith/` on every `smith build`; `smith_library(b, "text")` returns the source's path and `smith_object` compiles a `.smith` source as it does a `.c` (the header step and `-I .smith/include` are added for it):

```c
SmithObject text = {.compiler = "smith-cc", .source = smith_library(b, "text"), .output = "build/smith/text.o", .flags = flags};
smith_object(b, &text);
```

`flags` includes `-I .smith/include`, and the program's link and `.objects` include it. Tests of code that uses it link it in `smith_tests`' `.objects`. See the smith-build and smith-test skills.
