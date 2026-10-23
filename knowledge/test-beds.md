---
type: design
title: The test beds and the bugs checkers found
description: checkers, learner and game are the projects smith is tried on, and checkers' tests found four bugs fixed at their source.
tags: [test-beds, checkers]
---
# The test beds and the bugs checkers found

## checkers (`~/Documents/checkers`)

A small checkers game, men only, kept small enough to reason about and to prove correct. Its tests found these bugs,
which are fixed at their source (checkers' history up to 2d6bbfa still has them, as cases for `smith prove`):
- `get_user_move` tested `from_char <= 'H'` twice and never `to_char`, so column I read `cells[row][8]`.
- At end of input `get_user_move` looped forever: `scanf` returned `EOF` and nothing ended the loop.
- `get_valid_diagonals` and `get_valid_jumps` missed legal moves (`else if`; `col > 1`, `row > 1`, `col > 2` off by
  one): 419,348 over 200,000 random boards, and no illegal move generated.
- `apply_follow_up_jumps` recursed. It is now a loop bounded by three jumps a turn (men only move forward, two rows a
  jump), the bound CBMC proved.
