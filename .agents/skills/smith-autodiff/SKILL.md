---
name: smith-autodiff
description: "Using std::autodiff (import std::autodiff; autodiff::gradient, reverse, forward, dup, out, constant): Enzyme's gradients and tangents of a function. Load when code imports std::autodiff, on a question about gradients or automatic differentiation in .smith."
---

# Derivatives of a function: smith's autodiff

smith has [Enzyme](https://enzyme.mit.edu) (v0.0.303, LLVM's automatic differentiation) linked into its own linker. When a program is linked, Enzyme finds each call to `__enzyme_autodiff` (reverse mode, a gradient) or `__enzyme_fwddiff` (forward mode, a tangent), writes the derivative of the function it is given and replaces the call with it. The library is the raw interface (declarations, no source that defines them) and `gradient`, `map` and `zeros` built on it. It is `lib/std/autodiff.smith` (the module `std::autodiff`) and its generated header `<smith/autodiff.h>`; a program that imports or includes it is an **autodiff program**, and smith builds it differently (below).

**`autodiff::gradient(f, &x, rest...)`** (`.smith` only) is the gradient of `f(&x, rest...)` with respect to `x`, a value of `x`'s type; every other argument is held constant. What it allocates (a new array for each `[:]` field) comes from the current allocator (smith-mem), so the result belongs to the caller's region and nothing is freed:

```smith fragment
import std::autodiff;

typedef struct { F32 m; F32 b; } Model;                  // numbers only: nothing to allocate
typedef struct { F64 w[:]; F64 b; } Layer;                // `[:]` fields (smith-lang-arrays), structs of them, a `[:]` of structs
typedef struct { Layer layers[:]; F64 scale[2]; I32 steps; } Net;

F64 loss(Net *net, const Batch *batch) { ... }

Net g = autodiff::gradient(loss, &net, &batch);   // g.layers[j].w[i] = d loss / d net.layers[j].w[i]
autodiff::map(sgd, &net, &net, &g, rate);                    // net = sgd(net, g, rate), leaf by leaf; F64 sgd(F64 p, F64 g, F64 rate)
Net m = autodiff::zeros(net);                     // a struct of net's shape, all zero: an optimizer's state
```

- The gradient has the shape of `*x`: a float field is differentiated, an integer, bool or enum field comes back zero, a fixed array of floats is filled, a nested struct is walked, a `[:]` field (one dimension) is a new array of the same length from the current allocator, and each element of a `[:]` or a fixed array of structs is walked in turn. A pointer field is an error at your call that says to use `[:]`; so is a call `f` does not accept ("too few arguments ... in instance std::autodiff::gradient(...)").
- `f` is a function's name (`mod::loss` too), its first parameter a pointer and its result a float; the arguments after it are anything `f` takes (pointers there are read, never differentiated). A pointer to `const` is fine.
- **`autodiff::map(f, &out, &a, &b, ..., x, y)`** sets every float leaf of `out` to `f` of the matching leaves of any number of structs of the same type (`out` may be one of them), walking nested structs, fixed arrays and `[:]` fields (equal lengths, checked); other leaves stay as they are. A struct pointer is walked to its leaf and anything else (the learning rate, the step) is passed to every call as it is, in its place. `f` is an ordinary function of one float per struct and one value per number (a generic cannot be passed); an `F32` leaf converts to `f`'s `F64` parameters and back. Adam (learner's shape) is `map(moment1, &m, &m, &g, beta1); map(moment2, &v, &v, &g, beta2); map(adam, &p, &p, &m, &v, lr, t)` with `F64 adam(F64 p, F64 m, F64 v, F64 lr, I32 t)` computing the bias corrections from `t`; SGD is `map(sgd, &p, &p, &g, lr)`. `zeros(&arena, x)` is `x` with every number zero and every `[:]` a new array of its length.

The raw calls, which `gradient` is built on and C uses:

```smith
import std::autodiff;
import <math.h>;

F64 f(F64 x, F64 y) {
    return x * x * c::sin(y) + 3.0 * x * y;
}

F64 loss(const F64 *p, Usize n) {
    F64 total = 0.0;
    for (Usize i = 0; i < n; i++) {
        total += p[i] * p[i];
    }
    return total;
}

F64 gradient_x(F64 x, F64 y) {
    return autodiff::reverse((void *)f, autodiff::out, x, autodiff::out, y); // df/dx; the call returns a double
}

// A pointer is dup: the pointer, then its shadow, which receives the derivative (zero it first).
void loss_gradient(const F64 *p, F64 *dp, Usize n) {
    autodiff::reverse((void *)loss, autodiff::dup, p, dp, autodiff::constant, n);   // forward mode: autodiff::forward
}
```

`gradient`, `map` and `zeros` are generic, so C has none of them: from C, the names are Enzyme's own (the header declares them; `smith build` finds the library from the include; [smith-autodiff-c](../smith-autodiff-c/SKILL.md) has the table, and the `enzyme_interleave` form `gradient` uses). Also: [smith-autodiff-c](../smith-autodiff-c/SKILL.md), [smith-autodiff-build](../smith-autodiff-build/SKILL.md).
