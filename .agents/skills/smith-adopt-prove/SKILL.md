---
name: smith-adopt-prove
description: "Adopting smith, steps 5 and 6: add contracts and smith prove, then rename .c files to .smith one at a time and make each idiomatic. Load when a smith-built C project is ready for proofs or modules."
---

# Contracts, proofs and .smith

## 5. Contracts and proofs

For code whose correctness matters (parsers, move generators, bounds), write `smith_requires`, `smith_ensures` and `smith_invariant` (`#include <smith/contract.h>`) and run `smith prove` (load `smith-contract`). In build.c the program links smith's contract library (trace comes with it), and its compiles take `-I .smith/include`:

```c
const char *const *flags = smith_cat(b, project_flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr);
SmithObject contract = {.compiler = "smith-cc", .source = smith_library(b, "contract"), .output = "build/contract.o", .flags = flags};
smith_object(b, &contract);                 // a failed contract calls smith_failure, which is in trace; smith_program links trace too
```

- One file: `smith prove src/util.c -- -I .smith/include` (the flags after `--` are the include paths and defines). The project form, `smith prove` with no arguments, proves the sources of each `smith_program` together, and also proves a function that calls a function with an `smith_ensures`: `main` calling `printf` fails with `cannot be proved: a call of printf, which has neither a body nor a contract`, so prove the files whose functions have no I/O by name (`smith prove src/util.c -- ...`) and use the project form only where every caller is provable.
- A failed contract at run time prints `smith-trace` lines; `./app 2> log; smith symbolize --binary ./app log` shows the failing line and call chain (load `smith-trace`).
- Run the real thing: report the `N proved, M failed` line and, for a counterexample, the values.

## 6. Rename files to .smith, then make them idiomatic, one at a time

A `.smith` file is C: renaming `x.c` to `x.smith` changes nothing about what it means, and it is the first half of the step, never the whole of it. **Rename, then make it idiomatic (load `smith-idiom` before you touch the file):** sized types for the standard C ones, `import <h>;` and `c::` for `#include`, `arena::push(&a, T)` for `sizeof` and `alignof` passed to a function, `constexpr` for `#define`, the capability a function needs as a parameter for a raw call. Run the counts of the porting checklist in `smith-idiom` before and after; a file with a standard C type, an `#include` or a `#define` left is not done. Do one file per commit:

1. `mv src/main.c src/main.smith` (`git mv`); in build.c change that `.source` to the new name (the compiler stays `smith-cc`). `smith build`; the program is the same. Commit it as the rename. Then make it idiomatic (`smith-idiom`), build and test again, and commit that on its own.
2. A file with its own header becomes a module: `src/util.c` + `util.h` become `src/util.smith` starting `module util;`, functions you want others to call marked `export`, and `import util;` in the callers (`util::clamp(...)`, C names as `c::printf` after `import <stdio.h>;`). Remove the header. Callers still in C include the generated `.smith/include/util.h` (add `-I .smith/include` to their flags and the header to their `.implicit_inputs`). Load `smith-lang`, then `smith-lang-modules`, `smith-lang-c-interop` and `smith-lang-sized-types`; use `smith-lang-generics` where a function must work for several types or takes a type; `smith-idiom` says which of these a port needs.
3. `smith build` writes each module's header first and compiles after; a stale or cyclic import is reported before any compile (`smith: import cycle: a::x -> a::y -> a::x`).
4. `smith prove` with no arguments proves the `.smith` modules of the project too, as well as its C (the README's first program); to prove one module, `smith prove -v src/util.smith -- -I .smith/include`. Both name functions as written (`util::clamp`).
5. Tests in `.smith` are `tests/test_x.smith` files (load `smith-test`, ".smith tests"). The editor, formatter, linter and debugger all read `.smith` as written.

When every `.c` is a `.smith`, the project can drop the explicit sources and build by convention: `main.smith` at the root, modules found by `import`, and `smith build --dump-config` no longer needs a Makefile (load `smith-build-levels`).

Also in this family: [smith-adopt](../smith-adopt/SKILL.md), [smith-adopt-build](../smith-adopt-build/SKILL.md).
