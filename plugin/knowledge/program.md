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
import std.console

fn main():
    console.print("hello\n")
```

## Rules

- Blocks are indented under a `:`.
- A `#` starts a comment.

## Interactions

This page names no feature that meets another. Each concept page after it carries its own interactions section.

## Replaces from C

`#include <stdio.h>` and `int main(void) { printf("hello\n"); }`.
