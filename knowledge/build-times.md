---
type: design
title: Where smith's build time goes, measured on a cloud container (#2677)
description: Cold, warm, no-op and one-file-edit builds of smith at -O1 and -O0, the split by kind of step, the test cost at -O0, the link as the only part shared libraries could cut, the old front end against the new one, and the three biggest wins for the edit-and-rebuild loop.
tags: [build, performance]
---
# Where smith's build time goes

Measured on 2026-11-07 for #2677. Measurement only: nothing here changed `build.c` or `ci.yml`. The `-O0` trial ran on a scratch branch that was never merged.

**The machine.** One cloud container: 4 cores, 15 GB of memory, Linux 6.18, LLVM pack `23.1.3-r12`. The smith under test was built by the newest release (`stage0-801b2e6`, put first on `PATH` as the build skill says), on commit `801b2e6`. Every case ran 3 times; the tables give the median, and the three runs were within 1 s of each other except where the text says so. A Mac or a CI runner differs: `ci-hosted.md` gives smith 57 s warm there.

**The commands.** `smith build` timed with `date +%s.%N` around it; the executor's own log (`.smith/.smith_log`: start ms, end ms, mtime, hash, path per output) split by kind through the step's `description` in `.smith/build.ninja`. The log is append-only and each output keeps its last line, so a case's steps are the lines whose mtime changed since the case before. Prepare steps: cold = `rm -rf build`, everything in `.smith/` except `stage0/`, and `~/.cache/smith/steps` (the LLVM pack kept); warm = the same with the step cache kept; edit = one `/* ... */` line appended to the file, reverted with `git checkout` afterwards.

## The results at -O1 (what main builds today)

| Case | Median wall | What ran |
|---|---|---|
| Cold `smith build` | 76.6 s | everything; see the split below |
| `smith test --list` after the cold build | 40.4 s | builds the test runners and lists 4,302 tests |
| Warm `smith build` (cache full, `build/` and `.smith/` removed) | 18.6 s | nothing compiled; 99 header steps and 88 `.smith` object steps are served or rerun (about 16 s of step time), tree-sitter parser 4.4 s, link 1.7 s |
| `smith test --list` after the warm build | 33.9 s | |
| The first build right after a warm build | 5.6 s (8.7 s without a test listing in between) | see "The no-op is not a no-op" |
| A second build after that | 0.45 s | `no work to do` |
| Edit `front/types_expr.smith` | 9.3 s | its object 2.2 s, link 1.6 s, 4 header steps (`cimport_cache.h` 1.8 s), the rest is the floor below |
| Edit `runtime/executor.c` | 38.3 s | `smith.o` 31.4 s (executor.c is included by smith.c), link 1.6 s |
| Edit `smith.c` | 39.0 s | `smith.o` 32.2 s, link 1.5 s |
| Edit `shim/hook.cpp` | 17.4 s | `hook_shim.o` 8.9 s, link 1.6 s |
| Build after reverting an edit | 7.0 to 8.5 s | the edit's objects come from the cache; the floor remains |

**The split of a cold build by kind** (sum of step times, so more than the 76.6 s of wall, because steps overlap on 4 cores):

| Kind | Steps | Step time |
|---|---|---|
| C++ shims and the precompiled header (`shim.pch` 12.1 s, `tidy_shim.o` 17.6 s, `affine_shim.o` 12.5 s, `hook_shim.o` 8.9 s, ...) | 24 | 130.8 s |
| C (`smith.c` alone 31.4 s, `prove_ir.o` 16.9 s, `prove_alive.o` 7.9 s) | 12 | 35.7 s |
| `.smith` modules through the old lowering (`.o`) | 88 | 28.9 s |
| `.smith` to `.h` headers (old lowering) | 99 | 6.3 s |
| Link of smith (and `smith-cc`, `smith-c++`) | 4 | 1.8 s |
| Tree-sitter parser | 1 | 1.4 s |

The C++ shims are about 64% of the step time of a cold build and `smith.c` is one translation unit that is the longest path (31 s of the 74.6 s the executor spans).

## The results at -O0 (every `-O1` in `build.c` made `-O0`)

| Case | -O1 | -O0 | Saves |
|---|---|---|---|
| Cold | 76.6 s | 49.8 s | 26.8 s |
| Warm | 18.6 s | 19.6 s | none (the warm build compiles nothing) |
| First build after a warm one | 5.6 s | 9.5 s | none |
| Edit `front/types_expr.smith` | 9.3 s | 8.8 s | 0.5 s |
| Edit `runtime/executor.c` | 38.3 s | 11.6 s | 26.7 s |
| Edit `smith.c` | 39.0 s | 11.7 s | 27.3 s |
| Edit `shim/hook.cpp` | 17.4 s | 13.0 s | 4.4 s |
| `smith test --list` after cold | 40.4 s | 55.1 s | slower by 14.7 s |

`smith.o` goes from 31.4 s to 3.5 s and `hook_shim.o` from 8.9 s to 3.8 s. The warm build is unchanged because the step cache holds the objects, and a `.smith` module's compile is a small part of an edit.

**Tests run slower, and one part crashes, at -O0.** The part used for the comparison is `smith test --test 'front_p*,front_l*,front_t*'`: at -O1 it passes 660 tests in 2.5 to 2.8 s (3 runs). At -O0 the front test runner stops with a segmentation fault (`FAILED: [code=139] .smith/task/test-front`) in all 3 runs, and the part reports `FAILED` after 2.4 to 3.3 s, which is not a run time: no `-O0` run time of a test part could be measured. The likely cause is deeper frames without optimization (a stack overflow), but this was not investigated. The full `smith test` at -O1 took 526 s once in this container (one run, 1,640 tests passed and 2 failed, `front_lsp_semantic_test` and `front_cimport_cache_test`; the kernel killed a process for lack of memory): it is not a figure to rely on.

## The no-op is not a no-op

A build right after a build ran 5.6 s, not 0.45 s, when the build before it was a warm one (outputs restored from the step cache, #816). `-d explain` names the cause: `.smith/imports.stamp` and `tree-sitter/src/parser.c` are older than `.smith/program`, so the tree-sitter parser is generated again (4.4 s), with `cimport_cache.h` (1.8 s), the template headers, the link symlinks, the test headers and `tests/stub.c`. Six steps rerun. The build after that one is `no work to do` in 0.45 s, and so is every build after a cold build.

## Shared LLVM

Not done. The LLVM pack (`~/.cache/smith/llvm/23.1.3-r12/install/lib`) holds 338 static libraries and no shared one. A shared link needs LLVM, clang, lld, lldb and Z3 built again with their dylib options and a pack that holds them, which is a change to `build_llvm_run_macos` and `patches/` and a build of LLVM taking hours, not the hour the ticket allowed. The measurement says how little it would win: the link of smith (425 MB) takes **1.5 to 1.8 s** in every case above. Shared libraries could save at most that, per build, and cost a slower start for every `smith` process.

## The old front end against the new one

`smith cc -O0 -c` on each of the 21 files of `lib/std/*.smith` that the new front end passes (`smith build frontend-coverage -Dfiles=<lib/std/*.smith>`: 21 of 54 files pass; the rest fail at `check` or `parse`, mostly `unknown type __uint128_t` through `std.test`). Median of 5 runs each, warm, with the old lowering on the original file and `--front-end=new` on the fixed copy from `.smith/frontend-coverage/tree`:

| | Old lowering | `--front-end=new` |
|---|---|---|
| Sum over the 21 files | 667 ms | 1,476 ms |
| Smallest and largest file | `wrapping` 19 ms, `text` 76 ms | `wrapping` 11 ms, `stream` 280 ms |

The new front end is faster on the small files (`array`, `ascii`, `build`, `effects`, `option`, `result`, `wrapping`: 11 to 15 ms against 17 to 26 ms) and slower on the larger ones (`stream` 280 ms against 38 ms, `text` 206 against 76, `failing` 156 against 32, `mem` 152 against 56, `categorical` 142 against 34). These are per-file times with the compiler already warm; nothing here is a build step yet, so the whole-build effect is not measured.

## The three biggest wins for the edit-and-rebuild loop

1. **Stop compiling `smith.c` at -O1 in the dev build: 27 s on each edit of `smith.c` or a `runtime/*.c` file** (38.3 to 39.0 s down to 11.6 to 11.7 s), and 26.8 s off a cold build (76.6 to 49.8 s). `smith.o` is one translation unit of 31 to 32 s. Two ways reach it: `-O0` for the dev build (which first needs the segfault of the front test runner understood), or splitting `smith.c`'s includes into several objects so an edit recompiles one. Only the release build would use `-O1`.
2. **Remove the floor of every build: about 5.5 s of the 9.3 s of a `front/` edit.** The edit's own work is 3.8 s (an object 2.2 s, the link 1.6 s); the rest is steps that rerun every time (`cimport_cache.h` 1.8 s, the template and test headers, `tests/stub.c`) and the executor's start. The 0.45 s of a true no-op shows the floor can be that low. The same cause makes the first build after a warm one take 5.6 to 8.7 s (the tree-sitter parser regenerated because its outputs are older than `.smith/program`), the second 0.45 s.
3. **Compile the C++ shims at -O0 in the dev build: 4.4 s on each edit of `shim/hook.cpp`** (17.4 s down to 13.0 s) and most of the 26.8 s of a cold build (the shims are 131 of 205 step seconds). A shim edit would then cost 13 s, of which 8 to 9 s are the floor and the link.

Shared LLVM is not in the list: its ceiling is the 1.6 s link.

## Not measured

The CI Mac and the owner's Mac; the full `smith test` as a repeated measurement; the cost of the build program itself (`.smith/program`, 98 ms in the executor's log); a `-O0` test run (the crash above). The first -O1 build after switching back from the `-O0` branch took 74.7 s, as if the step cache did not serve the older -O1 objects: not investigated.
