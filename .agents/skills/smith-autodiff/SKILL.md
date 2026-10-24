---
name: smith-autodiff
description: "Using smith::autodiff (import smith::autodiff; autodiff::gradient, reverse, forward, dup, out, constant): Enzyme's gradients and tangents of a function. Load when code imports smith::autodiff, on a question about gradients or automatic differentiation in .smith."
---

# Derivatives of a function: smith's autodiff

smith has [Enzyme](https://enzyme.mit.edu) (v0.0.303, LLVM's automatic differentiation) linked into its own linker. When a program is linked, Enzyme finds each call to `__enzyme_autodiff` (reverse mode, a gradient) or `__enzyme_fwddiff` (forward mode, a tangent), writes the derivative of the function it is given and replaces the call with it. The library is the raw interface (declarations, no source that defines them) and `gradient`, `map` and `zeros` built on it. It is `lib/smith/autodiff.smith` (the module `smith::autodiff`) and its generated header `<smith/autodiff.h>`; a program that imports or includes it is an **autodiff program**, and smith builds it differently (below).

**`autodiff::gradient(&arena, f, &x, rest...)`** (`.smith` only) is the gradient of `f(&x, rest...)` with respect to `x`, a value of `x`'s type; every other argument is held constant. The arena comes first even when nothing is allocated:

```smith fragment
import smith::arena;
import smith::autodiff;

typedef struct { f32 m; f32 b; } Model;                  // numbers only: nothing to allocate
typedef struct { f64 w[:]; f64 b; } Layer;                // `[:]` fields (smith-lang-arrays), structs of them, a `[:]` of structs
typedef struct { Layer layers[:]; f64 scale[2]; i32 steps; } Net;

f64 loss(Net *net, const Batch *batch) { ... }

Net g = autodiff::gradient(&io.arena, loss, &net, &batch);   // g.layers[j].w[i] = d loss / d net.layers[j].w[i]
autodiff::map(sgd, &net, &net, &g, rate);                    // net = sgd(net, g, rate), leaf by leaf; f64 sgd(f64 p, f64 g, f64 rate)
Net m = autodiff::zeros(&io.arena, net);                     // a struct of net's shape, all zero: an optimizer's state
```

- The gradient has the shape of `*x`: a float field is differentiated, an integer, bool or enum field comes back zero, a fixed array of floats is filled, a nested struct is walked, a `[:]` field (one dimension) is a new array of the same length from the arena, and each element of a `[:]` or a fixed array of structs is walked in turn. A pointer field is an error at your call that says to use `[:]`; so is a call `f` does not accept ("too few arguments ... in instance smith::autodiff::gradient(...)").
- `f` is a function's name (`mod::loss` too), its first parameter a pointer and its result a float; the arguments after it are anything `f` takes (pointers there are read, never differentiated). A pointer to `const` is fine.
- **`autodiff::map(f, &out, &a, &b, ..., x, y)`** sets every float leaf of `out` to `f` of the matching leaves of any number of structs of the same type (`out` may be one of them), walking nested structs, fixed arrays and `[:]` fields (equal lengths, checked); other leaves stay as they are. A struct pointer is walked to its leaf and anything else (the learning rate, the step) is passed to every call as it is, in its place. `f` is an ordinary function of one float per struct and one value per number (a generic cannot be passed); an `f32` leaf converts to `f`'s `f64` parameters and back. Adam (learner's shape) is `map(moment1, &m, &m, &g, beta1); map(moment2, &v, &v, &g, beta2); map(adam, &p, &p, &m, &v, lr, t)` with `f64 adam(f64 p, f64 m, f64 v, f64 lr, i32 t)` computing the bias corrections from `t`; SGD is `map(sgd, &p, &p, &g, lr)`. `zeros(&arena, x)` is `x` with every number zero and every `[:]` a new array of its length.

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
    autodiff::reverse((void *)loss, autodiff::dup, p, dp, autodiff::constant, n);   // forward mode: autodiff::forward
}
```

`gradient`, `map` and `zeros` are generic, so C has none of them: from C, the names are Enzyme's own (the header declares them; `smith build` finds the library from the include; [smith-autodiff-c](../smith-autodiff-c/SKILL.md) has the table, and the `enzyme_interleave` form `gradient` uses). Also: [smith-autodiff-c](../smith-autodiff-c/SKILL.md), [smith-autodiff-build](../smith-autodiff-build/SKILL.md).
