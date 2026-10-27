---
type: design
title: A GPU kernel is a .smith function written as Metal source
description: A function whose first parameter is gpu::Thread * is written as Metal source by smith cc --smith-metal, which the Metal API compiles at run time or Apple's metal at build time; today one thread for each element and the C subset that Metal shares (spike of issue #18).
tags: [gpu, metal, language]
---
# A GPU kernel is a .smith function written as Metal source

Issue #18 plans a GPU layer for `.smith`. This is the first step, a spike: one kernel, the learner's `elementwise_gelu_kernel`, written as a `.smith` function (`tests/build_fixtures/gpu_metal/gelu_kernel.smith`) that runs on the GPU and gives the handwritten kernel's output bit for bit.

**Why Metal source.** The lead's decision is that the output is Metal source run through the Metal API, not Apple's AIR. The spike measured it (numbers in the pull request and in #18): the kernel runs the same speed from all three paths, because Apple's compiler makes the same code; and smith cannot make AIR itself, since the bitcode of smith's LLVM (23) is refused by Apple's `metallib` (`Invalid bitcode file`, tried on the round trip of Apple's own AIR), while Apple's reader is an older LLVM with typed pointers.

**What is built.** `lib/smith/gpu.smith` is the library `smith::gpu` (`gpu::Thread` with the thread's `index`). `smith cc --smith-metal kernel.smith [-o kernel.metal]` (`runtime/gpu_metal.c`, a small hook in `runtime/lang_lex.c`) takes the raw lowering of the file, the text `--smith-lower` prints, and writes Metal source for each function whose first parameter is `gpu::Thread *`:
- the body is copied from the lowering with names mapped (`__UINT32_TYPE__` is `uint`, `tanhf` is `tanh`, `fminf` is `fmin`): the subset that is the same in C and in Metal Shading Language;
- each pointer parameter is a `device` buffer and each other parameter a `constant` buffer, numbered from 0 in order after the thread (`buffer(0)` is `count` of the gelu kernel), so the host binds them in that order;
- the entry point has the lowered name (`gelu_kernel_gelu` for `gelu_kernel::gelu`), takes `thread_position_in_grid` and makes the `gpu::Thread`;
- a file-scope `constexpr` becomes a `constant`. A file with no kernel is an error and writes nothing.

**Limits today.** One thread for each element; no threadgroup memory, barriers, SIMD-group functions, `half`, slices or calls to other functions (a helper function is not written yet: inline it). A name outside the table fails in Metal's compiler, which names it. The host side (pipelines, buffers, encoding) is Objective-C or the planned `objc::` calls (#318), and is not smith's.

**Tests.** `tests/gpu_metal_test.smith`: the shape of the source, the error for a file with no kernel, and the run on the GPU against gelu in double precision (the learner's tolerance, `4e-6 (1 + |x|)`) and against a copy of the handwritten kernel (`handwritten.metal`).
