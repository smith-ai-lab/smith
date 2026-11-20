---
name: write-smith
description: Write Smith source the way the language is meant to be written, and not in the shape of another language. Use whenever you write or change a .smith file: a module of std/, the compiler in compiler/, an example, or a program in a test.
---

# Write Smith

Read `knowledge/idioms.md` before you write a line. It says which form of the language to reach for and names the shape from another language that each replaces. Then read the page of each feature you use, under `knowledge/language/`.

## While you write

- When you are porting from Rust or another language, port what the code does and not how it is laid out. A `&mut self`, a struct made to carry two results, a counted loop that never reaches its count, and a table of helper functions for constants are each named in `idioms.md` with what to write in their place.
- Check the `status` of each feature's page. Where the form `idioms.md` chooses is not built, write the nearest form that is. Do not invent a pattern to get around the gap.
- Write nothing the pages do not teach. If `knowledge/` is silent on something the work depends on, ask in a comment on the issue.

## When you are done

Say in the pull request, with the lines side by side:

- Each place you could not write the idiomatic form, the form you wrote, and the issue that covers the gap, or that none does.
- Each place where Smith made the code worse to write or to read than the code you ported from, even where it compiles. This is how `idioms.md` and the roadmap are kept right, so leave nothing out to make the work look finished.

If you found a form that is better than what `idioms.md` chooses, propose it in the pull request. Do not edit `idioms.md` yourself unless the issue asks you to.
