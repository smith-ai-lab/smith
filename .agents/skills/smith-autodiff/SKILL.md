---
name: smith-autodiff
description: "Using smith::autodiff (import smith::autodiff; autodiff::gradient, reverse, forward, dup, out, constant): Enzyme's gradients and tangents of a function. Load when code imports smith::autodiff, on a question about gradients or automatic differentiation in .smith."
---

# Derivatives of a function: smith's autodiff

smith has [Enzyme](https://enzyme.mit.edu) (v0.0.303, LLVM's automatic differentiation) linked into its own linker. When a program is linked, Enzyme finds each call to `__enzyme_autodiff` (reverse mode, a gradient) or `__enzyme_fwddiff` (forward mode, a tangent), writes the derivative of the function it is given and replaces the call with it. The library is the raw interface and nothing else: declarations, no source that defines them. It is `lib/smith/autodiff.smith` (the module `smith::autodiff`) and its generated header `<smith/autodiff.h>`; a program that imports or includes it is an **autodiff program**, and smith builds it differently (below).

**`autodiff::gradient(f, &x, rest...)`** (`.smith` only) is the gradient of `f(&x, rest...)` with respect to `x`, returned as a value of `x`'s type; every other argument is held constant:

```smith
import smith::autodiff;

typedef struct { f64 w[3]; f64 b; } Params;
typedef struct { f64 x[3]; f64 scale; } Batch;

f64 loss(Params *p, Batch *batch) {
    return p->b + batch->scale * p->w[0] * p->w[0] * batch->x[0];
}

Params model_gradient(Params *p, Batch *batch) {
    return autodiff::gradient(loss, p, batch); // g.w[0] = d loss / d p->w[0], and so on
}
```

- `f` is a function's name (`mod::loss` too), its first parameter a pointer and its result a `f64`; the arguments after it are anything `f` takes (none is fine; pointers there are read, never differentiated). A call `f` does not accept is an error at your call ("too few arguments ... in instance smith::autodiff::gradient(...)"), as is a first argument that is not a pointer.
- The result is zeroed and filled by reverse mode. **What `x` points to must be values that hold their own memory**: scalars, fixed arrays, structs of them. A struct of pointers (`double *w`) gets a gradient with null pointers: for that, call `autodiff::reverse` with the shadow struct built by hand (below).
- A pointer to `const` is fine (`const Params *`). The same Enzyme call as the raw form below: equal to the bit.

The raw calls, which `gradient` is built on and C uses:

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

`gradient` is generic, so C has no `gradient`: from C, the names are Enzyme's own (the header declares them; `smith build` finds the library from the include; [smith-autodiff-c](../smith-autodiff-c/SKILL.md) has the table, and the `enzyme_interleave` form `gradient` uses).

Also in this family: [smith-autodiff-c](../smith-autodiff-c/SKILL.md), [smith-autodiff-build](../smith-autodiff-build/SKILL.md).
