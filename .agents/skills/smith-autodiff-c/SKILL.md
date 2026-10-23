---
name: smith-autodiff-c
description: "smith::autodiff from C (#include <smith/autodiff.h>, __enzyme_autodiff, __enzyme_fwddiff, enzyme_dup, enzyme_out, enzyme_const) with the table of names and the rules about the markers. Load when C calls __enzyme_autodiff or on 'failed to find fn to differentiate'."
---

# autodiff from C

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

Also in this family: [smith-autodiff](../smith-autodiff/SKILL.md), [smith-autodiff-build](../smith-autodiff-build/SKILL.md).
