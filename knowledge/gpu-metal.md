---
type: design
title: A GPU kernel is a .smith function written as Metal source
description: A function whose first parameter is gpu::Thread * is written as Metal source by smith cc --smith-metal, with the helpers, structs and constants it reaches; the build makes a .metallib beside the program with Apple's metal when it resolves, or leaves the source for the program to compile (issue #18, #554).
tags: [gpu, metal, language]
---
# A GPU kernel is a .smith function written as Metal source

Issue #18 plans a GPU layer for `.smith`; #554 is its first full step: the learner's elementwise and reduce kernels (`tests/build_fixtures/gpu_metal/elementwise.smith`) are `.smith` functions that give the handwritten kernels' output bit for bit.

**Why Metal source.** The lead's decision is that the output is Metal source run through the Metal API, not Apple's AIR. The spike measured it (numbers in #18): the kernel runs the same speed from all three paths, because Apple's compiler makes the same code; and smith cannot make AIR itself, since the bitcode of smith's LLVM (23) is refused by Apple's `metallib` (`Invalid bitcode file`), while Apple's reader is an older LLVM with typed pointers.

**What is written.** `smith cc --smith-metal kernel.smith -o k.metal` (`runtime/gpu_metal.c`, a small hook in `runtime/lang_lex.c`) takes the raw lowering of the file, the text `--smith-lower` prints, reads its declarations, and writes Metal source:
- the kernels are the functions whose first parameter is `gpu::Thread *`; each must return `void`. Bodies are copied with names mapped (`__UINT32_TYPE__` is `uint`, `tanhf` is `tanh`, `fmaf` is `fma`): the subset that is the same in C and in Metal Shading Language;
- the entry point has the lowered name (`elementwise_gelu_kernel` for `elementwise::gelu_kernel`), takes every place Metal gives a thread (position in grid, in threadgroup, threadgroup in grid, grid size, SIMD lane, SIMD group, SIMD groups) and fills the `gpu::Thread` of `lib/smith/gpu.smith` (its fields are in the prelude of `gpu_metal.c`, in the same order);
- the parameters after the thread are `[[buffer(n)]]` from 0: a pointer is `device`, a `const` pointer to a struct is `constant` (the uniforms, read as `u->count`), any other parameter a `constant` reference;
- **reached declarations**: a function of the file named by a kernel (transitively), the structs and `constexpr` constants those name, are written once in file order; the rest is not. A helper's pointers get an address space the same way; it cannot take a `gpu::Thread`, since that makes it a kernel;
- `gpu::shared(t, T, n)` (lowered to a statement expression) becomes a `threadgroup T smith_shared_k[n]` declared first in the kernel, because Metal allows threadgroup variables only there; so it works in a kernel's own body and not in a helper. A local pointer declaration is rewritten `auto p = ...` so it takes the address space of its initializer;
- `gpu::barrier` is `threadgroup_barrier(mem_flags::mem_threadgroup)`, `gpu::simd_sum` is `simd_sum`, both defined in the prelude;
- a file with no kernel is an error and writes nothing.

**The library and the build.** `-o k.metallib` also builds: Apple's `xcrun metal -fno-fast-math` and `metallib` when `xcrun -f metal` resolves (the MetalToolchain is a separate download), otherwise the file is the Metal source. A program that loads it tells them by the first bytes (`MTLB`), and compiles source with `mathMode` safe and `mathFloatingPointFunctions` precise, which is what `-fno-fast-math` builds (tested: same bits). `smith_program` (`runtime/build_runtime.c`) adds a step for each linked source that has `import smith::gpu;` and `(gpu::Thread *`, writing `<folder of the program>/<file stem>.metallib`, which the link waits for; conventions and `build.c` alike need no call.

**Limits.** The structs a kernel names are defined in its file. No generic helpers, `half`, simdgroup matrices or function constants (#546, #547). The CPU can call a kernel for one thread with a `Thread` it fills (a threadgroup and SIMD group of one: `barrier` does nothing, `simd_sum` is the argument). The host side (pipelines, buffers, launching) is the next part of #554, on `objc::` calls (#318).

**Tests.** `tests/gpu_metal_test.smith`: the shape of the source, the helper written once and an unreached function not at all, shared memory and barriers, the refusals, the `.metallib` or source, the build step, and the run on the GPU of 13 kernels against the learner's handwritten ones (`handwritten.metal`), every output equal bit for bit, from source and through Apple's `metal`.
