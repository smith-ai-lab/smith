---
name: smith-autodiff-build
description: "How an autodiff program is built (full LTO, no passes, ASan but no UBSan in debug), what smith needs for it, and testing a derivative. Load on an 'Enzyme:' link error, 'needs a smith with Enzyme linked in', or when an autodiff program has no UBSan."
---

# How an autodiff program is built

`smith build`, `run`, `test`, `lint` and the other verbs find that a program (or the tests) reaches `std::autodiff` (a lexical scan of its imports and includes, as for every library) and then, in **every profile** (debug, release, fast, and the test runner):

- links with **full LTO** (`-flto=full`, never ThinLTO: Enzyme needs the function and its callees in one module, and under ThinLTO it fails with "failed to find fn to differentiate");
- compiles each file with the profile's `-O` level and **`-Xclang -disable-llvm-passes`**: Enzyme wants IR that is not yet optimized (after `-O2` the gradient is 3.1 times a hand-written one; before it, 1.1 to 1.3 times), and the link's LTO pipeline optimizes after Enzyme;
- puts the objects in `build/<profile>/autodiff/...`, beside the others, so a source the program shares with a program that does not differentiate (or with the tests) is compiled once each way and never conflicts;
- keeps **AddressSanitizer in the debug profile, run after Enzyme, and leaves out UndefinedBehaviorSanitizer**: each file is compiled with `-fsanitize=address -fno-sanitize-recover=address` (the profile's, less `undefined`) and `-Xclang -disable-llvm-passes`, so its functions carry `sanitize_address` and nothing is instrumented yet; at the full-LTO link smith's lld runs Enzyme and then AddressSanitizer (clang's debug options: use-after-scope, no recover, stack use-after-return decided by the runtime), before the optimizer. So an out-of-bounds read in a differentiated function is reported both where the function is called and inside its gradient (`diffe<name>`), with the source line (ASan sees what is left after Enzyme has simplified the gradient, so a gradient whose result nothing reads can lose the read; `smith test` reports the same way, symbolized). A module clang already instrumented is left alone, so ASan never runs twice. The link also passes `-Wl,-object_path_lto,build/debug/<name>.lto.o` (kept beside the program) so the debug map has the object with the lines. Any other `-fsanitize` flag of the project's own is dropped. What is lost: UBSan's checks and `-fno-sanitize-recover` for it. Why: UBSan's run-time handlers (`__ubsan_handle_*`) make Enzyme fail ("No create nofree of empty function"), and its trap mode builds but loses its checks inside the gradient (issue #59 tracks UB checks after Enzyme). Release and fast have no sanitizer. Cost, on the scan reference in debug: link 0.14 s against 0.10 s without ASan, and the run 0.7 s against 0.3 s. This is one function in the build runtime (`smith_autodiff_flags`), the place to change the policy.

A program that does not use the library is built exactly as before: its commands do not change. In debug a local that holds pointers (`ReferenceScanInputs in = {scan->x.data, ...}`) can make the link print `Enzyme: Mismatched activity for: store ptr inttoptr ...` warnings (the `-ftrivial-auto-var-init=pattern` store into a slot that later holds an active pointer); the gradient is right and the build passes.

Enzyme runs **inside smith's own lld**, linked statically (Enzyme is built by `smith build llvm` beside the LLVM install, and linked into the full smith): nothing is loaded from disk. A link with no Enzyme call costs nothing extra. The measured costs, on an M-series machine: a link of the scan reference with a gradient takes 172 ms against 44 ms for the same code with none; the gradient of learner's selective scan is 1.24 times the hand-written forward and backward together (release profile).

Not supported, and what happens:

- **The stage0, and a smith run with `SMITH_CLANG` or `SMITH_LINKER`** (an external clang or linker, which have no Enzyme): `smith build` of an autodiff program stops with `std::autodiff needs a smith with Enzyme linked in`. Build the full smith (`smith build llvm`, then `smith build` in smith's repository).
- **An object compiled without `-flto=full`** (a library you built by hand, an archive) has no bitcode: if the differentiated function or a callee is in it, the link fails with Enzyme's "failed to find fn to differentiate" or "No reverse pass found".
- **`smith cc` is a pure passthrough and adds no flag.** A command of your own links through smith's lld, so it differentiates too: `smith cc -O2 -flto=full -Xclang -disable-llvm-passes main.c kernel.c -o app` (one command compiles and links; with separate compiles, give each `-flto=full -Xclang -disable-llvm-passes` and the link `-flto=full`). Without `-flto=full` there is no bitcode for Enzyme to read.
- **A low-level `build.c`** that calls `smith_object` and `smith_program` itself writes those flags itself (`smith_library(b, "autodiff")` gives the declarations only); `build::profile_flags` and `build::source_flags` return the conventions' flags, which are never the autodiff ones.

## Testing a derivative

Check a gradient against a hand-written one or a finite difference, as the tests of smith do (`tests/autodiff/`): the toy compares each derivative with its analytic value, learner's scan reference compares Enzyme with the hand-written backward to 1e-12 relative (also through a struct of `F64[:]` fields), and an SGD and an Adam step written with `autodiff::map` are compared with a loop over the same numbers. A test that differentiates is an ordinary `test_*` file that imports `std::autodiff`; `smith test` builds it as an autodiff program.

Also in this family: [smith-autodiff](../smith-autodiff/SKILL.md), [smith-autodiff-c](../smith-autodiff-c/SKILL.md).
