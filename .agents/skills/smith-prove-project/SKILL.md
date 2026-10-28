---
name: smith-prove-project
description: "smith prove on files and on a project: `smith prove main.c util.c -- -I .smith/include`, `--function=`, `--budget=`, a .smith file named as written, the project form with no arguments (every smith_program, the log, the coverage line), a source the compile rejects. Load when choosing what to pass to smith prove or reading its summary."
---

# `smith prove`: files and projects

```sh
smith prove main.c util.c -- -I .smith/include -DDEBUG           # files, and the flags after -- for their compile
smith prove --function=clamp --budget=5 -v clamp.c -- -I .smith/include
smith prove                                                      # a project: every smith_program, C and .smith
smith prove -v src/util.smith -- -I .smith/include               # one module, functions named as written (util::clamp)
```

- **A `.smith` file** is compiled as `smith cc` compiles it (C2y, imports found among the files named, `import std::contract;` and `contract::requires(...)`), and its functions are named as written everywhere: `ok    geometry::box::clamp  (geometry/box.smith:5)`, `--function=geometry::box::clamp`, `the call of geometry::box::clamp may break smith_requires(low <= high)`, counterexample values `geometry::box::clamp.low = 10`. Lines are the `.smith` source's. A module's imports are other `.smith` files named in the same command, or headers on the include path.
- A project's `smith prove` (no arguments) proves the C and `.smith` sources of each program it builds (`smith_program`; not the tests' runner), together as the program links them, with the flags of their first compiles less what only instruments or caches (`-fsanitize`, modules, frame pointers, `-ftrivial-auto-var-init`, the depfile); it needs no task or list in build.c, and `smith check` runs it. A `.smith` project with no build file (the conventions) works the same. It prints one line (`smith prove: ok (19 proved, 0 failed, 59 trusted, 11.2 s)`) and writes the rest, the `trusted` lines included, to `.smith/log/prove.log`; a second line says the coverage, `4 of 23 functions have contracts; smith prove -v lists the 19 without` (the project's own functions: those with a contract of their own or `smith_trusted()`, not smith's libraries, not a generic instance of `contract::`), and `-v` lists each function without one as `without  name  (file:line)`; nothing fails on low coverage. A failure shows an excerpt and the log's path. `smith prove -v` (the cost report, printed in full), `--budget=SECONDS`, `--emit-ir=FILE`, `--no-cache` and `--function=NAME` (one function, wherever the program's sources define it, and the time is reported) are the project's options too. A source the compile rejects with the build's strict flags (a missing prototype) is reported by the compiler, not as "nothing to prove" alone: put the prototypes in a header. With no contract in the files it prints `smith: prove: nothing to prove: ...` and `0 of N functions have contracts`, and succeeds; a `--function=` that names no function the prover read is an error that names it (each one, before anything is proved), and a function with no contract is one too. A `static` function that nothing calls is not emitted by clang, so a harness written as one is absent: mark it `[[gnu::used]]`.

See `smith-prove`.
