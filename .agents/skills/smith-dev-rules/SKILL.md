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
- **One full `smith check` at a time** on a machine, and say plainly what you did not run.
- **A test's limits count CPU time, never wall time**: the machine is shared and its load reaches 100 or more; give a test of a CPU limit a large `--wall-timeout`. See [machine-load](../../../knowledge/machine-load.md).
- **Report friction when you finish.** Besides what you did, say what slowed you down or misled you: a skill that was wrong, missing or hard to find, an error message that did not say the fix, a rule you had to guess, a check that caught you (and whether its message was enough). Name the skill, message or check. Each report is how the skills and the checks get better, and whether a change to them helped shows in the next agent's report.
