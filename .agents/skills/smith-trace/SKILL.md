---
name: smith-trace
description: "Failures with a call stack: <smith/trace.h>, smith_failure, the smith-trace lines a failed contract, check or assert writes to standard error, smith symbolize, sanitizer reports with lines (llvm-symbolizer), and the debug/release/fast profiles' sanitizers. Load when a program prints 'smith-trace begin' lines, to turn a log into the failing source line, when running smith symbolize, when a build.c calls smith_library(b, \"trace\"), on 'undefined symbol: smith_failure', or on an AddressSanitizer or UBSan report without lines."
---

# Traces, `smith symbolize` and the sanitizers

This skill ships in the smith plugin, from `.agents/skills/smith-trace/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The header is `.smith/include/smith/trace.h`; read it first.

A program holds no symbolizer. When a contract, a check or an `assert`-like failure fires, it walks its stack by frame pointers and writes the addresses with each image's UUID and slide, as lines starting `smith-trace`. smith turns them into the failing line, the source text and the call chain with LLVM's symbolizer, reading the debug map of the binary (its object files, so build with `-g`; no dSYM and no `dsymutil` needed). A failure costs about 8 ms beyond smith's start.

A failure comes from a plain function call in a `.smith` file as it does in C. `src/util.smith`:

```smith
module util;

import <smith/contract.h>;
export i32 clamp(i32 x, i32 lo, i32 hi) {
    c::smith_requires(lo <= hi);
    return x < lo ? lo : x > hi ? hi : x;
}
```

and `smith-trace` lines of `clamp(15, 10, 0)`, turned into the line and the call chain:

```text
$ ./build/app 2> run.log                    # exit 134: abort
$ smith symbolize --binary build/app run.log
requires failed at src/util.smith:5
      5 |     c::smith_requires(lo <= hi);
called from
  src/util.smith:5      util::clamp
  src/main.smith:5      main
```

(From a program with `smith_requires(lo <= hi)` called as `clamp(15, 10, 0)`; the log held `smith-trace begin requires failed`, an `image` line with the binary's UUID, and `frame` lines.)

- `smith symbolize [--binary <program>] [<log>]` reads a file, a pipe or standard input. `--binary` names the program when the image's path in the log is gone or moved. A log from another run of the same binary works as long as the object files it names are still on disk. It needs the full smith (`smith symbolize --available` exits 3 on a stage0).
- `smith test` does this itself: the runner prints a failure's line, source and call chain, and `smith build test` pipes the runner through `smith symbolize`. For a runner started by hand: `build/tests --test name --trace | smith symbolize`.
- **Where it comes from.** `smith_failure(kind)` of `<smith/trace.h>` (kinds `SmithFailure_Check`, `_Requires`, `_Ensures`, `_Invariant`) is what `smith_requires`, `smith_ensures`, `smith_invariant` and the `smith_check*` functions call. Outside a test it writes the block to standard error and calls `abort()`. The failing line is the caller's: frames whose names start with `smith_failure`, `smith_contract_`, `smith_check`, `smith_requires`, `smith_ensures` or `smith_invariant` are skipped. So a check or contract is a plain function, never a macro.
- **Linking it.** A program that uses contracts links the trace library, and naming `smith_library(b, "contract")` is enough: `smith_program` brings trace (as `build/.../trace.o`, beside the contract object). To make trace's object yourself, `smith_object` it before the program, with `smith_library(b, "trace")`, and `smith_program` uses that one. `smith_tests` links it itself. Keep frame pointers (`-fno-omit-frame-pointer`; Apple's ABI keeps them).
- **Writing your own failure** (a library that cannot include `<smith/test.h>`; from `.smith`, `import <smith/trace.h>;` and `c::smith_failure(c::SmithFailure_Check)`): call `smith_failure(SmithFailure_Check)`; to write a block yourself, `smith_trace_capture` and `smith_trace_write(fd, indent, heading, site, details, frames, count, exact, names)` (the last is safe in a signal handler with `names` false).
- **Sanitizers.** The debug profile (`smith build`, or `smith new` projects) compiles with `-fsanitize=address,undefined -fno-sanitize-recover=all`. smith answers as `llvm-symbolizer` for the sanitizers' runtimes: the test runs make `.smith/bin/llvm-symbolizer`, a link to smith, and point `ASAN_SYMBOLIZER_PATH` at it; for a run by hand, do the same with a link of your own. So their reports have file and line. A sanitized binary records an absolute rpath to smith's runtime folder (`$XDG_DATA_HOME/smith/clang/llvm-<version>-<key>`, else `~/.local/share/...`), which `smith cache clean` and pruning never touch, so built binaries keep running; a deleted folder makes them fail at load, naming the missing library. A sanitized process starts as fast as Apple's (9 ms for an empty program).
- Release (`smith build release`) is `-O2` with assertions on and no sanitizers; fast (`smith build fast`) is `-O2 -DNDEBUG`: contracts and `assert` do nothing there.
- For a failure you cannot read from the trace, debug it: `smith-debug`.
