---
type: concept
title: A program
description: The smallest Smith program, a file with an imported module and a main function, which every other concept page builds on.
tags: [language, program]
---
# A program

A Smith program is one file with a main function. Names from the standard library come in with an `import`; nothing from `std` is visible without one.

## Example

```smith
pub fn main() -> i32:
    let answer = 6 * 7
    answer
```

The last line of a function is its value. Printing needs `import std.console`; that example joins this page when the new front end compiles the standard library from a user project (#2041, #1902).

## Rules

- Blocks are indented under a `:`.
- A `#` starts a comment.

## Interactions

This page names no feature that meets another. Each concept page after it carries its own interactions section.

## Replaces from C

`#include <stdio.h>` and `int main(void) { printf("hello\n"); }`.
