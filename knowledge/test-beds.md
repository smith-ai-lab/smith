---
type: design
title: The test beds and the bugs checkers found
description: checkers, learner and game are the projects smith is tried on, and checkers' tests found four bugs fixed at their source.
tags: [test-beds, checkers]
---
# The test beds and the bugs checkers found

## Where agents run them

Reading a test bed, or running the installed smith on it (to measure, or to reproduce a bug a user would see), is done in place. A smith under test (a branch's binary) runs only in a scratch copy: building, testing and proving write `.smith/` (objects, stamps, caches, `compile_commands.json`), and several lanes at once, each with a different smith, would overwrite each other's results and leave the owner's editor reading files made by a binary that never shipped. Edits that follow a smith change are made, committed and pushed on purpose, never left in the owner's working tree.

## checkers (`~/Documents/checkers`)

A small checkers game, men only, kept small enough to reason about and to prove correct. Its tests found these bugs,
which are fixed at their source (checkers' history up to 2d6bbfa still has them, as cases for `smith prove`):
- `get_user_move` tested `from_char <= 'H'` twice and never `to_char`, so column I read `cells[row][8]`.
- At end of input `get_user_move` looped forever: `scanf` returned `EOF` and nothing ended the loop.
- `get_valid_diagonals` and `get_valid_jumps` missed legal moves (`else if`; `col > 1`, `row > 1`, `col > 2` off by
  one): 419,348 over 200,000 random boards, and no illegal move generated.
- `apply_follow_up_jumps` recursed. It is now a loop bounded by three jumps a turn (men only move forward, two rows a
  jump), the bound CBMC proved.
