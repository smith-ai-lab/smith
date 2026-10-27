---
type: architecture
title: The checked allocator
description: mem::checked wraps an allocator and records the stack of every block, so a leak names its allocation site and a double free, a free of a foreign block and a use after free are reported; the test runner sets one around each test (issue #239, step D2a).
tags: [handlers, allocator, test, sanitizer]
---
# The checked allocator

`mem::checked(parent)` (`lib/smith/mem.smith`, C: `smith_mem_checked`) is an allocator, a `SmithAllocator` first in its
struct like the arena's, that makes its blocks from a parent (null: the system's) and keeps a record of each: the size,
a serial number, the stack of the allocation (16 frames) and, once freed, of the first free (8 frames). It is set like
any allocator, `mem::set_allocator(&checked.base)`, so `malloc`, `mem::alloc` and third-party C reach it
([handlers](handlers.md)); nothing is recorded, and nothing costs, in a program that never makes one.

- **A double free** (a free of a block it already freed) and **a free or realloc of a block it never made** (called on it
  directly: through the handlers a block that is not its own goes to libc, because a block made before the allocator was
  set is legitimate) stop with the failure `memory error` (`SmithFailure_Memory`): in a test the test fails, elsewhere
  the program aborts, and either way the allocation and the first free are written as `smith-trace` blocks that `smith
  symbolize` and `smith test` turn into source lines.
- **A leak** is a block still live when `smith_mem_checked_leak` / `checked_report` look: they list the live blocks in
  the order of allocation, so a report is the same on every run.
- **A freed block is kept, poisoned for AddressSanitizer** (`__asan_poison_memory_region`), until `checked_release`; its
  address cannot come round again and hide the mistake, and a read or write after the free is a `use-after-poison`
  report. A block is exactly the size asked, so a write past it meets the parent's own redzone (AddressSanitizer's
  `heap-buffer-overflow`; an arena's gap). For an arena as the parent, the poisoning of unused space and of everything
  after a `reset` is the arena's own (it poisons what is past the cursor), so a block used after its arena was reset is a
  `use-after-poison` report too.
- **The user's pointer is the parent's own block**, with the records kept apart. A block still live when the checked
  allocator is released is an ordinary block of the parent's, so code that frees it later (libc keeping a string it
  was given) frees it right.
- **The runner sets one around each test** (`checked_begin` / `checked_finish` in `lib/smith/test.smith`, around the
  test function alone, not the runner's own setup). A test that returns with a live block fails with `memory leaked` and
  the stack of the allocation, which the report shows as the line that allocated. `--unchecked` turns it off for a run.
  The runner links `mem` (`smith_tests_libraries`) and imports it by its header, `<smith/mem.h>` with the C names,
  because a module import of `smith::mem` makes the C names of its headers (`index`) errors in a file that uses them as
  variables.
- **The C library's own blocks are not leaks.** `libc` keeps some of what it allocates for the life of the process (the
  cache of its number printing, `__Balloc_D2A`). At the end of a test each live block is looked at with `dladdr`: walking
  out from the allocator past its own machinery (this module, the handlers, the system's allocator), if the first
  frame is in a system library and in a function that does not hand its block to the caller (`strdup`, `asprintf`,
  `fopen`, `getline`... do), the block is the library's and is not reported. A frame of the program's own ends the
  walk: the block is the caller's. Not run on Linux yet.
- **Costs and limits:** a block costs a record (about 300 bytes) and a stack walk; freed blocks are kept until the test
  ends, so a test that allocates and frees in a long loop holds all of them. A block a program hands to the C library to
  keep (`setenv`) is reported as a leak: that is what `--unchecked` is for.

## The failing allocator (step D2b)

`mem::failing(parent, key)` (`lib/smith/mem.smith`, C: `smith_mem_failing`) is an allocator over a parent (null: the
system's) that refuses some allocations, so a test reaches every allocation-failure path of the code under test and a
failure replays from its seed.

- **The decision is a draw from the key.** The key is used once, at creation (`key_bits`); after that the allocator holds
  a `u64` chain: each allocation or reallocation splits it with `random::split` into the decision and the next link. The
  decision for call n therefore depends on the key and n alone (it draws even when `one_in` is 0), and the same key with
  the same sequence of calls refuses the same allocations. There is no `key_derive`.
- **The schedule is `one_in`:** a field, 8 at creation (one allocation in eight), 1 for every one, 0 for none; `calls` and
  `refused` count. Free passes to the parent; `owns` and `size` ask it (a null parent owns nothing, so libc frees).
- **A refusal is a null from `alloc`:** `mem::try_alloc` and `try_alloc_raw` answer `.none` / null; `mem::alloc` stops
  with a checked failure naming the allocator ("mem::failing") and the size. Until tasks (#324) that aborts the process
  (the test runner ends the test); #324 makes it fail the task and unwind through `defer`.
- **The runner gives a test one:** `void test_x(random::Key key, mem::Failing *failing)` (`SmithKey key, struct
  SmithFailing *failing` in C) runs on `--seeds` keys like a property test. The key is split in two: the test gets the
  first half, the failing allocator the second, over the checked allocator (so what was not refused is still leak
  checked), installed as the thread's allocator for the call of the test alone. `--seed` replays it. `tests_list.h`
  declares `struct SmithFailing;` and does not include `<smith/mem.h>`, because a plain include before a `.smith` file
  that imports `smith::mem` hides the module's instance section.
- **`.checked_allocator`** is a `build::Setting` (`Default`, `On`, `Off`; zero is the default) on `build::AppTests` and
  `build::Tests`: `Off` passes `--unchecked` to the runner, so the tests run without `mem::checked` (and the
  `Failing` of a failing test then has the system's allocator as its parent). Tests are built in the debug profile,
  where the default is on; release and fast programs never make a checked allocator, so there is nothing to turn off
  there.
