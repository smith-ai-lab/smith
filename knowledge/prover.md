---
type: architecture
title: smith prove checks LLVM IR with Alive2, Bitwuzla and Z3
description: smith prove proves each function with a contract in a process of its own, assumes functions marked smith_trusted, and uses no list.
tags: [prove, alive2, bitwuzla, z3]
---
# smith prove checks LLVM IR with Alive2, Bitwuzla and Z3

**`smith prove` checks LLVM IR with Alive2, Bitwuzla and Z3, linked into every full smith.** Contracts are
the calls `smith_requires`, `smith_ensures`, `smith_invariant` and `smith_assert` of `<smith/contract.h>` (the module `smith::contract`). There is one LLVM configuration
(`~/.cache/smith/llvm/23.1.3-r6`): RTTI and exceptions on, which Alive2 needs, and the patches (`patches/*.patch`: the token hook #11, clang-format for `.smith` #25, debug names #27, XNU fast mmap, the editor's defaults (`smith-editor-defaults`), `.smith` as a source file in clang-format's main-include rule (`smith-format-source-extension`), the command hook for clangd and clang-tidy (`smith-command-hook`), clangd running `smith-*` checks (`smith-editor-fast-check`), C function bodies in the constant evaluator for `.smith` (`clang-constexpr-c`)); the compiler's
output is byte-identical with and without RTTI and exceptions (only the install path in debug info differs). `smith build
llvm` also builds, static and from packages pinned by hash, with nothing from Homebrew (Ninja, GMP, MPFR, Meson, CaDiCaL
and symfpu are among them): Z3 5.3.1, Alive2 (commit 01a5ec4, the newest that builds against 23.1.3) and Bitwuzla 0.9.1
(`scripts/prove-libs.sh`, `scripts/bitwuzla.sh`). Only the stage0 has no prover. Each function is proved in a process of
its own, which ends at its budget (SIGALRM) and when its parent dies. Bitwuzla decides the queries on every core, cheap query first and the full one only if that fails; Z3 only finds a counterexample's values, because on queries
with many stores through one array Z3 takes minutes where Bitwuzla takes a fraction of a second. Skill:
smith-contract.

**The prover's boundary is `smith_trusted()`, and there is no list** (issue #42, owner's decision: no proof file or
function list in build.c). `smith prove` proves every function with a contract of its own, minus the trusted ones;
`main` and functions with no contract are not proved. A function whose first statement is `smith_trusted()` (a call, so
it reads from the IR and works in C and `.smith` alike) is assumed: callers prove its `smith_requires` and assume its
`smith_ensures`, its body is never read, and it is memory-assumed to write only through the pointers it is given. The
report lists it as `trusted` and counts it apart from `proved`. A proved function that calls io, inline assembly or a
libc function with no contract fails with a message that names the call and the function to mark. Errors name their
cause (a `smith_requires` that reads after a write names both lines), and `--emit-ir=FILE` writes the IR the prover
checks. Skill: smith-contract.

**A pointer a function returns under `smith_ensures` is a whole object of its type, aligned, or null** (issue #74). Smallest sound rule found: the function carries `dereferenceable_or_null(sizeof pointee)` and `align` on its return, so Alive2 proves it at each `ret` (a pointer one past the end, or to a local, is undefined behaviour there); its callers get the same as an assumption on the call's value (a null result is replaced, for the assumption only, by an object that is there, so it holds for both), and the loaded values are given memory, as a parameter's are. The caller still needs `smith_ensures(result != nullptr)` to load through it. A trusted function is assumed, never proved, so its result gets nothing. A pointer to `void`, or to a type with no size in the debug info, gets nothing. A pointer into memory that a later call frees is outside what smith has (it has no `free`). Trusted and edge functions are `nofree`, whatever pointers they are given.

**A program's proof reads smith's libraries for their contracts** (issue #66, item 3's first part): `.smith/build.info` has a `library` line after each program's `unit` line, naming the library sources the program links (not io's, whose exports are `smith::edge`, nor contract's or test's), and `smith prove` passes them as `--library=FILE`, compiled with the program's flags and linked in, every function of them marked `smith.library`: used by callers through its contract (or read as a helper), never a unit, never listed as trusted. An `smith::edge` export loses its body there, so it stays the edge. Why not contracts in the generated headers: a contract is an expression over parameters that reads memory, and only the compiler's IR says what it means; the header would need a second, lossy copy of it. Libraries proved once and cached by content, and the cost of reading library bodies, are the rest of #66.

Soundness is tested by running what was proved (random inputs satisfying each `requires`, under the sanitizers) and by a corpus of false claims that must fail: `tests/prove_sound.sh`, `tests/prove/sound/`, `tests/prove/unsound/` (issue #64). A local nothing has written holds a frozen value, not `undef`, so an uninitialized read is never proved to be what the contract wants.
