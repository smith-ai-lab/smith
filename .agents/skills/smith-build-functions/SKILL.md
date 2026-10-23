---
name: smith-build-functions
description: "C functions as build steps: .run = fn, .args, smith_run_lines, smith_run_status, .stamp for a check that only passes, and what a failed step prints. Load when a build needs a check, a generated file or a data-baking step written in C inside build.c."
---

# C functions as steps

- C functions as steps: `.run = fn` (with `.command` empty, and `.args` for the function) makes a step or task call `int fn(SmithRun *run)` while the build runs, for checks that read build outputs. It returns 0 for success, anything else fails the step with that status. `run->inputs`, `run->outputs`, `run->args` are the step's lists and `run->b` works with every helper that needs no `build()` state. Like a command, a function writes the outputs it declares; a check declares none, and smith's stamp records that it passed. Editing build.c reruns every `.run` step.
- **A function's data is its step's lists, nothing else.** The step's command is `.smith/program --run <function> --input ... --output ... --arg ...`; `build()` and the `smith_defer` callbacks do not run for it, and the `-D` options are not passed. Put what the function needs in `.inputs`, `.outputs` and `.args` (an option's value goes in `.args`: `.args = smith_list(b, smith_option_value(b, "who", "nobody", "a name"), nullptr)`). A `.run` step costs about the cost of starting a process.
- Inside the function, `smith_run_lines(run, argv)` runs a program with no shell and returns its stdout lines, or prints `smith: <argv[0]> failed (status N)` and returns `nullptr`; `smith_run_status(run, argv, &lines)` returns the status (127 when it cannot start) and prints nothing. `smith_run_program(run, argv, environment)` runs a program with no shell and its stdout and stderr going straight to the terminal; `environment` is a list of `NAME=value` entries (or `nullptr`) added to the current environment, and it returns the status the same way (127 when it cannot start). All three flush stdout first, so the function's own printing comes first. `smith_write` plus `.restat` stops what follows from rerunning when the bytes are the same.
- `smith_run_combined(run, argv, &lines)` is `smith_run_status` with the program's stderr in the same stream as its stdout, in the order written (`2>&1`), for comparing a program's whole output with an expected text.
- A failed `.run` step prints `smith: <description>: failed (status N)` as its last line (its first output when it has no `.description`), so give a check a `.description`. `smith_run_depend(run, path)` records a file the function read, which it learned at run time; the step must have a `.depfile`, which smith writes when the function returns, so a change to that file reruns the step.
- `.stamp = "build/check/a.stamp"` is the output of a step whose result is only that it passed: smith touches it when the command succeeds or the function returns 0 (no `sh -c '... && touch'`), and `.depfile` may name it as its target (`-MT`). It cannot be combined with `.outputs`, and a task cannot have one. A step with no outputs and no `.stamp` gets a stamp named by a hash of its inputs, so two such steps with equal inputs and different `.run` functions (or commands) would write the same stamp: smith stops and says so ("this step has no outputs and the same inputs as an earlier step ... give one of them a distinct .stamp"). Give each its own `.stamp = ".smith/stamp/<name>"`, and a `.description` to tell them apart in messages. A syntax check is `{.command = {"smith-cc", flags..., "-fsyntax-only", "-MMD", "-MF", "x.d", "-MT", "x.stamp", "x.c"}, .inputs = {"x.c"}, .stamp = "x.stamp", .depfile = "x.d"}`.
- Not implemented yet: `.content_hash`; it fails with "not implemented yet". If the build needs it, tell the owner.

```c
static int check_nm(SmithRun *run) { // fail if an object uses a symbol outside .args
    const char *const *used = smith_run_lines(run, (const char *[]){"nm", "-uj", run->inputs[0], nullptr});
    if (used == nullptr) return 1;
    int failed = 0;
    for (const char *const *s = used; *s; s++) {
        if (strcmp(*s, run->args[0]) != 0) { fprintf(stderr, "%s uses %s\n", run->inputs[0], *s); failed = 1; }
    }
    return failed;
}
// in build(): SmithStep check = {.run = check_nm, .args = (const char *[]){"_puts", nullptr},
//     .inputs = obj.outputs, .outputs = (const char *[]){"build/check.stamp", nullptr}}; smith_step(b, &check);
```

Also in this family: [smith-build](../smith-build/SKILL.md), [smith-build-smith](../smith-build-smith/SKILL.md), [smith-build-c](../smith-build-c/SKILL.md), [smith-build-objects](../smith-build-objects/SKILL.md), [smith-build-makefile](../smith-build-makefile/SKILL.md).
