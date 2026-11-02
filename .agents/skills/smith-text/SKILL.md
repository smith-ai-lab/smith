---
name: smith-text
description: "Reading small text formats with std::text (import std::text): lines and words in memory. Load when code includes <smith/text.h> or parses replays, scripts, configs or logs by hand."
---

# Small text formats: smith's text

Text is a `char[:]` (a pointer and a count), read-only input a `const char[:]`, never a terminated string, so a file read into memory is read where it lies. The library calls no C library function and no locale (its checks are `contract::assert`, so it links the contract and trace libraries, which `smith_program` brings), and every loop is bounded by a length. A number written here reads back as the same value on any machine. The header is `.smith/include/smith/text.h`; read it first.

**text is a `.smith` module** (`module std::text;`, `lib/std/text.smith` in smith's repository, issue #39), and its C header is generated from it. From `.smith` it is `import std::text;` and the names are the module's: `text::make(...)`, `text::take_line(&t, &line)`, `text::parse_u64(word, &x)`, `text::number_capacity`. C keeps its API under "From C". Compile the module with a smith that has clang inside (the `.smith` source is `.smith/lib/std/text.smith`; a stage0 cannot compile it, though C code that only includes the header needs nothing). `smith cc main.smith lib/std/text.smith -I include` builds a single command.

## Read lines and words

```smith
import std::text;

// A replay: "turn -90 5" lines. Returns false at the first bad line.
Bool read_replay(const char input[:]) {
    const char line[:];
    const char word[:];
    while (text::take_line(&input, &line)) {         // a line without its newline, a trailing \r, or a # comment
        if (!text::take_word(&line, &word)) continue; // blank, or only a comment
        if (text::is(word, "turn")) {
            F32 degrees;
            U64 ticks;
            if (!text::take_word(&line, &word) || !text::parse_f32(word, &degrees)) return false;
            if (!text::take_word(&line, &word) || !text::parse_u64(word, &ticks)) return false;
        }
    }
    return true;
}
```

A file read into memory is `const char text[:] = ((const char *)data)[0:size];`, a string literal converts as it is (`const char text[:] = "a terminated string";`, count without the zero), and `text::make(string)` is the text of a terminated `const char *`. `take_line` and `take_word` take pointers to slices (`const char (*text)[:]`, skill `smith-lang-slice-pointers`) and advance the first.

- A word is a run without spaces or tabs. `text::take_line` strips a `#` comment and the spaces before it, so `#` cannot appear in a word.
- `text::is(word, "move")` compares a word to a terminated string, exactly.
- `text::copy(word)` copies a word into the current allocator with a NUL after `count`; `mem::free` it. `text::terminated(word)` is the same copy as a `const char *`, for a C boundary only. `text::os_error(code)` names a raw errno as a constant text ("an unknown error" for the rest). They replace `strdup` and `strerror`.

## Replays must round-trip

Whatever the writers produce, the parsers read back exactly: every u64, i64 and hex value, including 0, the maximum, the minimum and leading zeros, and every finite float written with 9 significant digits (`%.9g`), signs of zero included. `lib/std/text_test.smith` checks it with property tests on keys, and `smith build text-exhaustive` parses all 2^32 float patterns, in two formats each, against `strtof`, and 2^24 sampled double patterns against `strtod`, on all cores.

Also in this family: [smith-text-numbers](../smith-text-numbers/SKILL.md), [smith-text-c](../smith-text-c/SKILL.md).
