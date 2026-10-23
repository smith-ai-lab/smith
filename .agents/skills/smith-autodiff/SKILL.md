---
name: smith-autodiff
description: "Using smith::autodiff (import smith::autodiff; <smith/autodiff.h> from C), Enzyme's derivatives of a function: autodiff::reverse and autodiff::forward (__enzyme_autodiff, __enzyme_fwddiff) and the markers autodiff::dup, out, constant (enzyme_dup, enzyme_out, enzyme_const). Load when code imports smith::autodiff or includes <smith/autodiff.h> or calls __enzyme_autodiff, when a build.c calls smith_library(b, \"autodiff\"), on a question about gradients, derivatives or automatic differentiation in C or .smith, or on `Enzyme:` errors, 'failed to find fn to differentiate', 'needs a smith with Enzyme linked in', or why an autodiff program has no UBSan and links with full LTO."
---

# Derivatives of a function: smith's autodiff

This skill ships in the smith plugin, from `.agents/skills/smith-autodiff/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

smith has [Enzyme](https://enzyme.mit.edu) (v0.0.303, LLVM's automatic differentiation) linked into its own linker. When a program is linked, Enzyme finds each call to `__enzyme_autodiff` (reverse mode, a gradient) or `__enzyme_fwddiff` (forward mode, a tangent), writes the derivative of the function it is given and replaces the call with it. The library is the raw interface and nothing else: declarations, no source that defines them. It is `lib/smith/autodiff.smith` (the module `smith::autodiff`) and its generated header `<smith/autodiff.h>`; a program that imports or includes it is an **autodiff program**, and smith builds it differently (below).

```smith
import smith::autodiff;
import <math.h>;

f64 f(f64 x, f64 y) {
    return x * x * c::sin(y) + 3.0 * x * y;
}

f64 loss(const f64 *p, usize n) {
    f64 total = 0.0;
    for (usize i = 0; i < n; i++) {
        total += p[i] * p[i];
    }
    return total;
}

f64 gradient_x(f64 x, f64 y) {
    return autodiff::reverse((void *)f, autodiff::out, x, autodiff::out, y); // df/dx; the call returns a double
}

// A pointer is dup: the pointer, then its shadow, which receives the derivative (zero it first).
void loss_gradient(const f64 *p, f64 *dp, usize n) {
    autodiff::reverse((void *)loss, autodiff::dup, p, dp, autodiff::constant, n);
}

f64 tangent(f64 x, f64 y) {
    return autodiff::forward((void *)f, autodiff::dup, x, 1.0, autodiff::dup, y, 0.0); // df in the direction (1, 0)
}
```

From C, the names are Enzyme's own (the header declares them; `smith build` finds the library from the include):

```c
#include <smith/autodiff.h>

double gradient_x(double x, double y) {
    return __enzyme_autodiff((void *)f, enzyme_out, x, enzyme_out, y);
}
void loss_gradient(const double *p, double *dp, size_t n) {
    __enzyme_autodiff((void *)loss, enzyme_dup, p, dp, enzyme_const, n);
}
```

| `.smith`                     | C                     | meaning                                                                          |
| ---------------------------- | --------------------- | -------------------------------------------------------------------------------- |
| `autodiff::reverse(f, ...)`  | `__enzyme_autodiff`   | reverse mode: the gradient                                                       |
| `autodiff::forward(f, ...)`  | `__enzyme_fwddiff`    | forward mode: the tangent of the result                                          |
| `autodiff::constant`         | `enzyme_const`        | the next argument is not differentiated                                          |
| `autodiff::dup`              | `enzyme_dup`          | a pointer (reverse: then its shadow, which receives the derivative) or a value and its tangent (forward) |
| `autodiff::out`              | `enzyme_out`          | a scalar whose derivative the call returns (reverse mode)                        |
| `autodiff::dup_no_need`      | `enzyme_dupnoneed`    | as `dup`, for a result the caller never reads                                    |

What to know:

- **The functions are variadic and return `double`** (the first derivative of an `out` argument, or a forward tangent). Nothing after the first argument is type checked, so pass exactly the types the function takes: a `float` after a marker is promoted to `double` and Enzyme refuses it ("Cannot cast ... primal argument"). For a struct of several derivatives, or another result type, call through a cast pointer type: `((Pair (*)(void *, ...))__enzyme_autodiff)(...)`. The first argument is the function as a `void *`.
- **The markers are never defined.** They are `extern int` that no source gives a value: a defined one would be folded to its value before Enzyme reads it. The library has no object that defines them; do not define them in a program either.
- **The differentiated function and everything it calls must be in the program's own code** (any file, `.smith` or C, across modules), not in a library with no bitcode (libm's `sin` is fine: Enzyme knows it; a function of another dylib is not). "No reverse pass found for f" names a call to a function whose code Enzyme never saw.
- **An error from Enzyme is a link error** (`ld64.lld: error: ... Enzyme: ...`, with the source line of the call and its context), since that is when it runs. It prints nothing otherwise.

## How an autodiff program is built

`smith build`, `run`, `test`, `lint` and the other verbs find that a program (or the tests) reaches `smith::autodiff` (a lexical scan of its imports and includes, as for every library) and then, in **every profile** (debug, release, fast, and the test runner):

- links with **full LTO** (`-flto=full`, never ThinLTO: Enzyme needs the function and its callees in one module, and under ThinLTO it fails with "failed to find fn to differentiate");
- compiles each file with the profile's `-O` level and **`-Xclang -disable-llvm-passes`**: Enzyme wants IR that is not yet optimized (after `-O2` the gradient is 3.1 times a hand-written one; before it, 1.1 to 1.3 times), and the link's LTO pipeline optimizes after Enzyme;
- puts the objects in `build/<profile>/autodiff/...`, beside the others, so a source the program shares with a program that does not differentiate (or with the tests) is compiled once each way and never conflicts;
- keeps **AddressSanitizer in the debug profile, run after Enzyme, and leaves out UndefinedBehaviorSanitizer**: each file is compiled with `-fsanitize=address -fno-sanitize-recover=address` (the profile's, less `undefined`) and `-Xclang -disable-llvm-passes`, so its functions carry `sanitize_address` and nothing is instrumented yet; at the full-LTO link smith's lld runs Enzyme and then AddressSanitizer (clang's debug options: use-after-scope, no recover, stack use-after-return decided by the runtime), before the optimizer. So an out-of-bounds read in a differentiated function is reported both where the function is called and inside its gradient (`diffe<name>`), with the source line. A module clang already instrumented is left alone, so ASan never runs twice. The link also passes `-Wl,-object_path_lto,build/debug/<name>.lto.o` (kept beside the program) so the debug map has the object with the lines. Any other `-fsanitize` flag of the project's own is dropped. What is lost: UBSan's checks and `-fno-sanitize-recover` for it. Why: UBSan's run-time handlers (`__ubsan_handle_*`) make Enzyme fail ("No create nofree of empty function"), and its trap mode builds but loses its checks inside the gradient (issue #59 tracks UB checks after Enzyme). Release and fast have no sanitizer. Cost, on the scan reference in debug: link 0.14 s against 0.10 s without ASan, and the run 0.7 s against 0.3 s. This is one function in the build runtime (`smith_autodiff_flags`), the place to change the policy.

A program that does not use the library is built exactly as before: its commands do not change.

Enzyme runs **inside smith's own lld**, linked statically (Enzyme is built by `smith build llvm` beside the LLVM install, and linked into the full smith): nothing is loaded from disk. A link with no Enzyme call costs nothing extra. The measured costs, on an M-series machine: a link of the scan reference with a gradient takes 172 ms against 44 ms for the same code with none; the gradient of learner's selective scan is 1.24 times the hand-written forward and backward together (release profile).

Not supported, and what happens:

- **The stage0, and a smith run with `SMITH_CLANG` or `SMITH_LINKER`** (an external clang or linker, which have no Enzyme): `smith build` of an autodiff program stops with `smith::autodiff needs a smith with Enzyme linked in`. Build the full smith (`smith build llvm`, then `smith build` in smith's repository).
- **An object compiled without `-flto=full`** (a library you built by hand, an archive) has no bitcode: if the differentiated function or a callee is in it, the link fails with Enzyme's "failed to find fn to differentiate" or "No reverse pass found".
- **`smith cc` is a pure passthrough and adds no flag.** A command of your own links through smith's lld, so it differentiates too: `smith cc -O2 -flto=full -Xclang -disable-llvm-passes main.c kernel.c -o app` (one command compiles and links; with separate compiles, give each `-flto=full -Xclang -disable-llvm-passes` and the link `-flto=full`). Without `-flto=full` there is no bitcode for Enzyme to read.
- **A low-level `build.c`** that calls `smith_object` and `smith_program` itself writes those flags itself (`smith_library(b, "autodiff")` gives the declarations only); `build::profile_flags` and `build::source_flags` return the conventions' flags, which are never the autodiff ones.

## Testing a derivative

Check a gradient against a hand-written one or a finite difference, as the tests of smith do (`tests/autodiff/`): the toy compares each derivative with its analytic value, and learner's scan reference compares Enzyme with the hand-written backward to 1e-12 relative. A test that differentiates is an ordinary `test_*` file that imports `smith::autodiff`; `smith test` builds it as an autodiff program.
