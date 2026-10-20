---
name: smith-build
description: "Running, writing and editing a project's build.c with smith build. Load when a project has a build.c at its root, when running smith build, on any message that starts with smith: build.c:, when something under .smith/ comes up, or when replacing a Makefile with build.c."
---

# Build with smith build

This file is copied from `.agents/skills/smith-build/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there, then copy it again.

`build.c` describes the build in C, explicitly and with no defaults. `smith build` compiles it, runs it, and runs ninja on the manifest it writes. The API is https://github.com/adam-r-kowalski/smith/blob/main/include/smith/build.h (after a first `smith build`, the same file is in `.smith/include/smith/build.h`); the design and its reasons are https://github.com/adam-r-kowalski/smith/blob/main/docs/build-c.md. Read those only when this file is not enough.

## Run it

- `smith build` builds the defaults. `smith build <name>...` builds those targets or runs those tasks. Run it from the folder that holds `build.c`.
- `-Dname` and `-Dname=value` set build.c's options for this call only. An option build.c does not declare is an error that lists the declared ones.
- Every other argument goes to ninja: `-j N`, `-n`, `-v`, `-k N`, `-t targets`, `-t clean` (removes every declared output), `-t compdb` (prints compile commands). `-f` and `-C` are refused.
- Each call compiles `build.c` into `.smith/program` when build.c, a file it includes or smith changed, runs it, which writes `.smith/build.ninja` when its bytes changed, and then runs ninja. So globs, environment variables and options are read fresh every time. A no-op takes a few milliseconds.
- `.smith/` is smith's own folder and ignores itself in git. Never edit it. `rm -rf .smith` is always safe; the next build recompiles build.c and reruns every step.
- `SMITH_VERBOSE=1` prints the commands smith runs. `SMITH_NINJA` picks the ninja; otherwise it is the first `ninja` on PATH.

## Write it

```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep app = {
        .command = (const char *[]){"smith", "-std=gnu23", "-g", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"app.c", nullptr},
        .outputs = (const char *[]){"app", nullptr},
    };
    smith_step(b, &app);
    SmithStep run = {.command = (const char *[]){"./app", nullptr}, .inputs = app.outputs, .pool = "console"};
    smith_task(b, "run", &run);
    smith_default(b, app.outputs);
}
```

- Declare a step, then pass its address. Afterwards its lists point at smith's own copies, so `app.outputs` can feed later steps.
- A list ends in `nullptr` and is never `nullptr` itself; the empty list is `(const char *[]){nullptr}`. `smith_cat(b, a, c, nullptr)` joins lists.
- `.command` is an argv, run with no shell. The whole words `$in` and `$out` become the step's inputs and outputs; every other word is passed exactly as written. For pipes, redirections or `&&`, write `{"sh", "-c", "<script>", ...}`.
- The fields: `.inputs` ($in; a change reruns the step), `.implicit_inputs` (a change reruns it, not on the command line), `.order_only_inputs` (built first, a change does not rerun it), `.outputs`, `.depfile` (the file `-MMD -MF` writes), `.description`, `.pool` (`"console"` gives the terminal), `.restat` (an unchanged output does not rerun what depends on it), `.always` (runs every build).
- A file the build generates and a compile reads with `#include` or `#embed` goes in that compile's `.implicit_inputs`; the depfile alone misses it on a clean build.
- `smith_task(b, name, &step)` is a target run by name, every time it is asked for, like make's `.PHONY`. A task has no outputs, `.restat` or `.always`. A step with no outputs instead reruns only when its inputs change.
- `smith_alias(b, name, targets)` names a group. `smith_default(b, targets)` is required; an empty list builds nothing. `smith_pool(b, name, depth)` limits a set of steps.
- Options: `smith_option(b, name, description)` for `-Dname`, `smith_option_value(b, name, fallback, description)` for `-Dname=value`. A make `VAR ?= x` is `smith_option_value(b, "VAR", smith_env(b, "VAR", "x"), "...")`.
- The outside world: `smith_env`, `smith_glob` (sorted; no `**`) and `smith_capture` (runs an argv with no shell and returns its output). `smith_capture` runs on every `smith build`, so a slow command that only some targets need belongs inside those steps' commands, as `$(...)` in `sh -c`.
- Helpers: `smith_fmt` (printf), `smith_map(b, items, "out/%s.o")`, `smith_when(b, condition, items)`, `smith_replace_extension(b, "src/a.c", ".o")`.
- `b->os` and `b->arch` are exactly what `uname -s` and `uname -m` print: `"Darwin"`, `"arm64"`.
- Not implemented yet: `smith_defer`, `smith_lines`, `.run`, `.args`, `.content_hash` and `smith_run_lines`. Each fails with "not implemented yet". If the build needs one, tell the owner.

## Replace a Makefile

- Keep every command word for word and every output path the same; a linked binary records its objects' paths.
- Build with make and with `smith build` from clean in the same folder, and compare the outputs before deleting the Makefile. Binaries built with `-g` in one compile-and-link step differ between two make builds too (a temporary object's name), so compare their machine code (`otool -tV` or `objdump -d`).
- make runs one recipe at a time without `-j`; ninja runs independent steps in parallel. `smith build -j1` gives make's order.

## When something goes wrong

- `smith: build.c: <step or task>: <message>` is a mistake in build.c. Fix build.c.
- A compiler error in build.c means build.c does not compile; ninja prints clang's message.
- `ninja: error: ...` is ninja's own, for example an input that no step makes and that does not exist.
- `smith: no build.c in this folder`: run from the project's root. `smith: cannot find ninja`: install ninja or set `SMITH_NINJA`.
- If smith build itself seems wrong, or smith is missing or broken, stop. Run the command again with `SMITH_VERBOSE=1` and report it to the owner. Do not work around it.
