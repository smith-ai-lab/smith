---
name: smith-gpu
description: "GPU kernels in .smith: a function whose first parameter is gpu::Thread * becomes Metal source; helpers, uniform structs, gpu::shared, gpu::barrier, gpu::simd_sum, the .metallib the build makes beside the program, smith cc --smith-metal. macOS only. Load when a .smith file imports smith::gpu, or on 'a kernel returns void', 'no kernel: a kernel is a function whose first parameter', 'gpu::shared(t, T, n) is for the body of a kernel'."
---

# GPU kernels from .smith

A kernel is an exported function whose first parameter is a `gpu::Thread *`. `smith cc --smith-metal k.smith -o k.metallib` writes Metal source for it and builds it; `smith build` does that for every module a program links that has `import smith::gpu;` and a function starting `(gpu::Thread *`, with no call in `build.c`, into `<folder of the program>/<file name>.metallib`.

```smith
module scale;

import smith::gpu;

export struct Uniforms {
    u32 count;
    f32 alpha;
};

static f32 twice(f32 x) {                     // a helper: written to the Metal file once, if a kernel reaches it
    return 2.0f * x;
}

export void scale(gpu::Thread *t, const Uniforms *u, const f32 *a, f32 *out) {
    if (t->index < u->count) out[t->index] = twice(a[t->index]) * u->alpha;
}
```

## What a kernel is

- **Parameters** after the thread are the buffers, numbered from 0 in order (`[[buffer(n)]]`): a pointer is a `device` buffer, a `const` pointer to a struct is `constant` (the uniforms), any other parameter a `constant` value. The function returns `void`.
- **The thread** (`gpu::Thread`): `index`, `index_y` (position in the grid), `local`, `local_y` (in the threadgroup), `group`, `group_y` (the threadgroup in the grid), `groups`, `groups_y` (the grid's size), `lane`, `simd`, `simds` (SIMD group). A 1-D dispatch has `y` 0.
- **Calls**: `gpu::shared(t, f32, 32)` (threadgroup memory; a count the compiler knows, in the kernel's own body, at most 32 KB), `gpu::barrier(t)`, `gpu::simd_sum(t, x)`; the float functions as `c::tanhf`, `c::expf`, `c::logf`, `c::sqrtf`, `c::fmaf`, `c::fminf`, `c::fmaxf`, `c::fabsf`, `c::powf`, `c::floorf`; functions of the same file (helpers take values and buffers, not a `gpu::Thread`); file-scope `constexpr` and structs they name.
- **A local pointer** (`const f32 *row = a + n;`) is written with `auto`, so it takes its address space from the buffer.
- Write float literals as `1.0f`; Metal has no `double`. Structs must be defined in the kernel's file. The sums keep their written order: no atomics.
- **Not yet**: the host side (pipelines, buffers, launching), generic helpers, `half`, matrices, function constants (issue #18, #554, #546).

## The library

`--smith-metal k.metal` writes the source; `k.metallib` is built by Apple's `metal` (`-fno-fast-math`) when `xcrun -f metal` resolves (the MetalToolchain is a separate download) and is the source itself when not; its first bytes (`MTLB` or `#include`) say which, and a program that loads it compiles source with `mathMode` safe and `mathFloatingPointFunctions` precise, which gives the same bits. The kernel's Metal name is its C name, `scale_scale` for `scale::scale`.

Reference: [knowledge/gpu-metal.md](../../../knowledge/gpu-metal.md).
