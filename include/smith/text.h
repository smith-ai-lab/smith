// <smith/text.h>: reading and writing small text formats by hand, such as replays, scripts, configs and logs. It calls
// no C library function and no locale, so a number written here reads back as the same value on any machine. Text is a
// pointer and a length, never a terminated string, so a file read into memory is read where it lies, and every loop is
// bounded by a length.
//
//   SmithText text = smith_text_make("move 3 4\nturn -90 # left\n");
//   SmithText line, word;
//   while (smith_text_take_line(&text, &line)) {
//       if (!smith_text_take_word(&line, &word)) continue;      // a blank line
//       if (smith_text_is(word, "move")) { ... }
//   }
//
// A parse that fails returns false and writes nothing to its result. A write never fails: the caller gives room for
// smith_text_number_capacity characters.
#pragma once

#include <stddef.h>
#include <stdint.h>

// A run of characters: all of a file, one line of it, or one word of a line.
typedef struct {
    const char *begin;
    size_t length;
} SmithText;

// The text of a terminated string.
[[nodiscard]] SmithText smith_text_make(const char *string);

// Takes the next line off text into line, without its newline, a trailing carriage return, or a # comment and the
// spaces before it. Returns false once text is empty.
[[nodiscard]] bool smith_text_take_line(SmithText *text, SmithText *line);

// Takes the next word off line, a run without spaces or tabs, into word. Returns false when only spaces are left.
[[nodiscard]] bool smith_text_take_word(SmithText *line, SmithText *word);

// Whether word is exactly the terminated string.
[[nodiscard]] bool smith_text_is(SmithText word, const char *string);

// Whole numbers: decimal digits, with an optional leading - for the signed one, or lowercase hexadecimal digits.
// Leading zeros of any number are fine. Each returns false for an empty word, a stray character (a +, a space, a
// capital hex digit) or a value that doesn't fit.
[[nodiscard]] bool smith_text_parse_u64(SmithText word, uint64_t *value);
[[nodiscard]] bool smith_text_parse_i64(SmithText word, int64_t *value);
[[nodiscard]] bool smith_text_parse_hex(SmithText word, uint64_t *value);

// A decimal number: an optional sign, digits with an optional fraction (-90, 0.25, .5, 5.), and an optional exponent
// (1e-5, 2.5E+3). The result is the float nearest to the number, ties to even, as strtof gives in the C locale, for
// any number of digits; "-0" is negative zero. It returns false for no digits, a stray character, or a number too
// large for a float (it would round to infinity). One too small rounds to zero or to a subnormal. No inf or nan.
[[nodiscard]] bool smith_text_parse_f32(SmithText word, float *value);

// Writes a number into out, which has room for smith_text_number_capacity characters, and returns how many it wrote.
// There is no terminator.
constexpr size_t smith_text_number_capacity = 20;
size_t smith_text_write_u64(char *out, uint64_t value);
size_t smith_text_write_i64(char *out, int64_t value);
size_t smith_text_write_hex(char *out, uint64_t value); // 16 digits, so every hash lines up
