---
name: write-smith
description: Read what knowledge/ says about the Smith you are about to write, before you write it, and write it as those pages teach and not in the shape of another language. Use whenever you write or change a .smith file: a module of std/, the compiler in compiler/, an example, or a program inside a test.
---

# Write Smith

This skill holds no rule of Smith. `knowledge/language/` holds them, one page for each feature, and this skill says how to use those pages.

## Steps

1. Read `knowledge/language/overview.md`, then `knowledge/language/index.md`, which lists every feature with a line on what its page holds. Open the page of each feature you are about to use, and follow the links it gives. For a module of the library, open its page under `knowledge/library/`.
2. Read the `status` and the `built` list at the top of each page. Write only what is built. A page whose status is `planned` describes Smith as it will be, and none of it compiles yet.
3. Write what the code does, in the forms those pages teach. When you port from another language, port its behaviour and not its layout.
4. Say in the pull request, with the lines side by side: each place the form a page teaches is not built and what you wrote in its place, with the issue that covers the gap, or that none does; and each place Smith made the code worse to write or to read than what you ported from, even where it compiles.

If the pages do not say how to write something the work depends on, ask in a comment on the issue. Do not invent a form.

## Where Smith is unlike what you know

Habit from another language goes wrong in these places first. Each line says only where to read.

- There are no references: a value is lent or taken, and never shared. `take.md`, `ownership.md`
- An effect in a signature is a permission, and state that several functions change is kept by a handler. `effects.md`, `handlers.md`
- The type of a whole number is a range and arithmetic is exact, so nothing is checked when the program runs and nothing is saturated to quiet the checker. `number-types.md`, `arithmetic.md`
- There is no `while`, and every loop is shown to end. `loop.md`, `open-loop.md`
- A name is defined once and never again where it can be used. `bindings.md`
- Nothing is imported without being written, and the prelude holds only what the syntax itself means. `use.md`, `prelude.md`
- Nothing is null and nothing is thrown. `try.md`
- The syntax is fixed and what it means comes from traits. `traits.md`, `operators.md`
