---
name: smith-dev-rules
description: "The rules every agent follows when changing smith: test the smith under test, scratch folders, no pkill, idiom, capabilities, one full check, and the friction report. Load at the start of any change to smith."
---

# smith-dev-rules

The decisions are in [AGENTS.md](../../../AGENTS.md); these are the habits.

- **Test the smith under test.** Run a change's tests with `build/<OS>-<arch>/smith`, not the installed `smith`, when the change is to the language, a library or the build. A test that builds a project puts the tested smith's folder (its `smith-cc` and `smith-c++`) first on `PATH`, so nothing in it compiles with the installed smith.
- **Scratch folders are your own.** Make each with `mktemp -d` (under your scratchpad when you have one), never a fixed name such as `proj`: another agent may be using it.
- **Write idiomatic Smith, not C renamed.** In a `.smith` file: sized types (`u64`, `usize`, `f64`), `import <h>;` with `c::` names, modules that import modules, generics where a type was passed as `sizeof` and `alignof`, and no preprocessor. Count them before and after a port: a module with a standard C type, an `#include` or an `#if` left is not done.
- **Effects come through capabilities.** No raw call to the world outside a capability's own module (`smith-effects`); a function takes the part of `io::Io` (`SmithIo` in C) it needs as a parameter.
- **Never signal a process that is not your own unwaited child**, and never `pkill` or `killall`: a reparented pid may already be another test's compile.
- **`host->read_header` returns a buffer valid only until the next read**, and the lowering's own lookups read others: copy what you need before calling anything that may read a header.
- **Never read `tree-sitter/src/`** (`parser.c` is about 185,000 lines, `grammar.json` and `node-types.json` thousands): tree-sitter generates it from `tree-sitter/grammar.js`, which is the file to read and edit. Regenerate it, and to check one rule, `grep` it for a line.
- **CI runs the full `smith check`; you run targeted tests.** Push your branch and open a pull request with `gh pr create`, the issue linked (`Closes #N`); run only the tests for what you changed (the corpus for the lowering, `test-prove` for the prover, one script or `--test` for a library). The lead merges when the `check` job is green on the head commit. See [ci](../../../knowledge/ci.md).
- **A full check by hand** (rarely) is `lockf /private/tmp/smith-full-check.lock build/Darwin-arm64/smith check`, one at a time, its output in a file; say plainly what you did not run.
- **An implementer never runs `smith build llvm` and never touches `~/.cache/smith/llvm`.**
- **A test's limits count CPU time, never wall time**: the machine is shared and its load reaches 100 or more; give a test of a CPU limit a large `--wall-timeout`. See [machine-load](../../../knowledge/machine-load.md).
- **Report friction when you finish.** Besides what you did, say what slowed you down or misled you: a skill that was wrong, missing or hard to find, an error message that did not say the fix, a rule you had to guess, a check that caught you (and whether its message was enough). Name the skill, message or check. Each report is how the skills and the checks get better, and whether a change to them helped shows in the next agent's report.
