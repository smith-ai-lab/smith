---
name: smith
description: "The map of smith, a C toolchain in one binary: every verb and which skill covers it. Load when a message starts with smith:, when you run any smith command, or when unsure which smith skill applies."
---

# smith

smith is a C toolchain written in C: clang, clang-format, clang-tidy, clangd, lld and lldb linked into one binary, and a build system whose build file is C or `.smith`. Its top level is only verbs, so `smith foo.c` is an error that says to run `smith cc foo.c`. This map says which small skill to load; each skill loads only when its subject comes up. If smith is missing from PATH or seems broken, stop and tell the owner; never swap in another compiler, rerun with `SMITH_VERBOSE=1` and report the command and its output.

## By what you are doing

- Adopting smith in a C project with a Makefile, CMake or plain `.c` files: [smith-adopt](../smith-adopt/SKILL.md), then `-build` and `-prove`.
- Starting a project: [smith-new](../smith-new/SKILL.md). Then `smith run`, `smith test`, `smith check`.
- Writing, editing, reviewing or porting a `.smith` file: **[smith-idiom](../smith-idiom/SKILL.md) first**, then [smith-lang](../smith-lang/SKILL.md). A `.smith` file that is C renamed is not done.
- Changing what the build does: [smith-build-levels](../smith-build-levels/SKILL.md), then [smith-build](../smith-build/SKILL.md).

## The verbs

| Verb | Load |
| --- | --- |
| `smith cc`, `smith c++`, `smith-cc`, `smith-c++` | [smith-cc](../smith-cc/SKILL.md); shaders [smith-metal](../smith-metal/SKILL.md) |
| `smith build`, `build.c`, `build.smith` | [smith-build](../smith-build/SKILL.md): [-smith](../smith-build-smith/SKILL.md), [-c](../smith-build-c/SKILL.md), [-objects](../smith-build-objects/SKILL.md), [-functions](../smith-build-functions/SKILL.md), [-makefile](../smith-build-makefile/SKILL.md), [-tests](../smith-build-tests/SKILL.md), [-levels](../smith-build-levels/SKILL.md) ([-high](../smith-build-levels-high/SKILL.md), [-low](../smith-build-levels-low/SKILL.md)), [-warnings](../smith-build-warnings/SKILL.md) |
| `smith fetch`, `smith cache`, `smith_package`, `smith_modules` | [smith-packages](../smith-packages/SKILL.md), [smith-modules-cache](../smith-modules-cache/SKILL.md) |
| `smith run`, `smith test` | [smith-run-test](../smith-run-test/SKILL.md); writing tests [smith-test](../smith-test/SKILL.md) |
| `smith check`, `fmt`, `lint` | [smith-check](../smith-check/SKILL.md), [smith-check-lints](../smith-check-lints/SKILL.md) |
| `smith lsp` | [smith-lsp](../smith-lsp/SKILL.md); Neovim [smith-neovim](../smith-neovim/SKILL.md) |
| `smith docs` | [smith-docs](../smith-docs/SKILL.md), [smith-docs-check](../smith-docs-check/SKILL.md) |
| `smith prove` | [smith-prove](../smith-prove/SKILL.md): [-project](../smith-prove-project/SKILL.md), [-slices](../smith-prove-slices/SKILL.md), [-calls](../smith-prove-calls/SKILL.md), [-pointers](../smith-prove-pointers/SKILL.md), [-loops](../smith-prove-loops/SKILL.md), [-budget](../smith-prove-budget/SKILL.md), [-ir](../smith-prove-ir/SKILL.md), [-trusted](../smith-prove-trusted/SKILL.md), [-cost](../smith-prove-cost/SKILL.md); contracts [smith-contract](../smith-contract/SKILL.md) ([-roles](../smith-contract-roles/SKILL.md)) |
| `smith dbg`, `smith dap`, `smith mcp` | [smith-debug](../smith-debug/SKILL.md), [smith-mcp](../smith-mcp/SKILL.md) |
| `smith symbolize`, `smith-trace` lines, sanitizer reports | [smith-trace](../smith-trace/SKILL.md) |
| `smith new`, `smith version` | [smith-new](../smith-new/SKILL.md); `smith version` prints the version and the commit |

## The language

Start at [smith-lang](../smith-lang/SKILL.md) (its table of steps); each feature has one skill that opens with a snippet.

- Modules and C: [-modules](../smith-lang-modules/SKILL.md), [-modules-errors](../smith-lang-modules-errors/SKILL.md), [-exports](../smith-lang-exports/SKILL.md) (`export`, `c_name`, `export inline`), [-c-interop](../smith-lang-c-interop/SKILL.md) (`import <h>;`, `c::`), [-c-headers](../smith-lang-c-headers/SKILL.md) (a module from C and C++), [-tools](../smith-lang-tools/SKILL.md) (`--smith-lower`, fmt, lint, editor).
- Types and arrays: [-sized-types](../smith-lang-sized-types/SKILL.md), [-arrays](../smith-lang-arrays/SKILL.md) (slices `T xs[:]`), [-array-dims](../smith-lang-array-dims/SKILL.md) (`f32 xs[:][2]`), [-array-params](../smith-lang-array-params/SKILL.md) (`i32 xs[4]`), [-slice-qualifiers](../smith-lang-slice-qualifiers/SKILL.md) (`const`, `restrict` on a slice), [-slice-pointers](../smith-lang-slice-pointers/SKILL.md) (`const char (*p)[:]`), [-tuples](../smith-lang-tuples/SKILL.md) (`struct { i32, f32 }`).
- Used once: [-affine](../smith-lang-affine/SKILL.md) (`[[smith::affine]]`, a second use is a compile error).
- What a function does to the world: [-effects](../smith-lang-effects/SKILL.md) (`[[smith::pure]]`: inferred, and a compile error where the promise breaks).
- Cleanup on every exit: [-defer](../smith-lang-defer/SKILL.md) (`defer stmt;` in a module, C2y's defer TS); [-try](../smith-lang-try/SKILL.md) (`try f()` returns a result's or option's failure, `errdefer` runs only on failure).
- Compile time: [-constexpr](../smith-lang-constexpr/SKILL.md) (a function run in the compiler; [-modules](../smith-lang-constexpr-modules/SKILL.md)), [-generics](../smith-lang-generics/SKILL.md) (`constexpr type T`, `auto`, `typeof`; [-auto](../smith-lang-generics-auto/SKILL.md), [-more](../smith-lang-generics-more/SKILL.md)), [-if-constexpr](../smith-lang-if-constexpr/SKILL.md), [-rest](../smith-lang-rest/SKILL.md) (`auto... rest`, `for constexpr`; [-expand](../smith-lang-rest-expand/SKILL.md)), [-reflection](../smith-lang-reflection/SKILL.md) (`fields(T)`; [-field](../smith-lang-reflection-field/SKILL.md)), [-type-values](../smith-lang-type-values/SKILL.md) (`constexpr type t`; [-more](../smith-lang-type-values-more/SKILL.md)), [-target](../smith-lang-target/SKILL.md) (no preprocessor, `target::`).
- Contracts and their proofs: [smith-contract](../smith-contract/SKILL.md), [smith-contract-proof](../smith-contract-proof/SKILL.md) (`modifies`, `invariant`).
- Style: [smith-idiom](../smith-idiom/SKILL.md), [-before](../smith-idiom-before/SKILL.md), [-after](../smith-idiom-after/SKILL.md), [-port](../smith-idiom-port/SKILL.md).

## The libraries

Each leads with its `.smith` use; `-c` skills are the C names and the build.c lines.

- [smith-arena](../smith-arena/SKILL.md) ([-c](../smith-arena-c/SKILL.md)), [smith-random](../smith-random/SKILL.md) ([-distributions](../smith-random-distributions/SKILL.md), [-own](../smith-random-own/SKILL.md), [-c](../smith-random-c/SKILL.md)), [smith-mem](../smith-mem/SKILL.md), [smith-math](../smith-math/SKILL.md), [smith-option](../smith-option/SKILL.md), [smith-result](../smith-result/SKILL.md), [smith-format](../smith-format/SKILL.md), [smith-text](../smith-text/SKILL.md) ([-numbers](../smith-text-numbers/SKILL.md), [-c](../smith-text-c/SKILL.md)).
- [smith-http](../smith-http/SKILL.md) (the package fetcher's HTTPS client), [smith-json](../smith-json/SKILL.md) ([-pull-write](../smith-json-pull-write/SKILL.md)), [smith-inflate](../smith-inflate/SKILL.md) ([-sink](../smith-inflate-sink/SKILL.md)), [smith-autodiff](../smith-autodiff/SKILL.md) ([-c](../smith-autodiff-c/SKILL.md), [-build](../smith-autodiff-build/SKILL.md)).
- [smith-test](../smith-test/SKILL.md) ([-modules](../smith-test-modules/SKILL.md), [-checks](../smith-test-checks/SKILL.md), [-workers](../smith-test-workers/SKILL.md), [-c](../smith-test-c/SKILL.md)), [smith-contract](../smith-contract/SKILL.md) ([-c](../smith-contract-c/SKILL.md)).
- [smith-io](../smith-io/SKILL.md): [-capabilities](../smith-io-capabilities/SKILL.md), [-errors](../smith-io-errors/SKILL.md), [-files](../smith-io-files/SKILL.md), [-processes](../smith-io-processes/SKILL.md), [-async](../smith-io-async/SKILL.md), [-net](../smith-io-net/SKILL.md), [-fake](../smith-io-fake/SKILL.md), [-c](../smith-io-c/SKILL.md); the check that enforces it: [smith-effects](../smith-effects/SKILL.md).

What smith is, in depth, is in [knowledge](../../../knowledge/index.md).
