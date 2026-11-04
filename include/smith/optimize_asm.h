// <smith/optimize_asm.h>: the asm behind std::optimize, the optimization barrier. Each function returns its argument
// unchanged, but the optimizer must treat the result as an unknown value and the argument as used. The bodies are
// extended asm, which a .smith file cannot hold (the front end rejects it), so they are written once in
// lib/optimize_asm.c and both front ends call them: std::optimize reads these declarations. Use `optimize::opaque`
// from a .smith file, and these from C.
#pragma once

#include <stdint.h>

// A float goes through the register class that keeps it out of memory on aarch64 ("w"), and "X" (any operand)
// elsewhere.
double smith_opaque_f64(double x);
float smith_opaque_f32(float x);
uint32_t smith_opaque_u32(uint32_t x);
uint64_t smith_opaque_u64(uint64_t x);
// A pointer to anything, in a general register.
const void *smith_opaque_ptr(const void *x);
