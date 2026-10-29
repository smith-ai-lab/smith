---
name: smith-ascii
description: "Using smith::ascii (import smith::ascii; ascii::space, digit, alpha): tests of one character, ASCII only and no locale, in place of isspace, isdigit and isalpha. Load when code imports smith::ascii, on classifying a char in .smith, or on a trim, a split or a token loop."
---

# Tests of one character: smith's ascii

This skill ships in the smith plugin, from `.agents/skills/smith-ascii/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`smith::ascii` (`lib/smith/ascii.smith`) holds three functions of a `char`, each answering the same on every machine because none reads the locale: `ascii::space(c)` (a space, tab, newline or carriage return), `ascii::digit(c)` (`0` to `9`) and `ascii::alpha(c)` (`a` to `z`, `A` to `Z`). They replace `isspace`, `isdigit` and `isalpha`, which are macros in some libcs, take an `int` and depend on the locale.

```smith
module words;

import smith::ascii;

// How many runs of letters a text holds.
export usize word_count(const char text[:]) {
    usize words = 0;
    bool inside = false;
    for (char c : text) {
        bool letter = ascii::alpha(c);
        if (letter && !inside) words += 1;
        inside = letter;
    }
    return words;
}

export bool blank(const char text[:]) {
    for (char c : text) {
        if (!ascii::space(c)) return false;
    }
    return true;
}
```

- A function of a `char` is the shape a predicate has, so a later `slice::trim(xs, ascii::space)` takes it as written (not built yet, #199).
- Not UTF-8: a byte of 128 or more is none of the three.

Also in this family: [smith-slice](../smith-slice/SKILL.md).
