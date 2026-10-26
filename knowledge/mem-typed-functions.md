---
type: contract
title: Typed memory functions, and the raw ones are an error in a module
description: smith::mem has copy, move, zero, fill, equal, bytes and view over slices, and the clang-tidy check smith-mem reports C's memcpy, memmove, memset and memcmp in a module.
tags: [mem, lint, idiom]
---
# Typed memory functions, and the raw ones are an error in a module

**C's memory functions take a `void *` and a byte count, so the type and the length travel apart from the data, the footgun behind most buffer bugs** (owner, 2026-10-26: "I like our type safe APIs on top of unsafe C APIs"). `smith::mem` (`lib/smith/mem.smith`, skill smith-mem) has typed generics that lower to the C functions: `mem::copy(dst, src)` and `mem::move` (slices of one element type, equal lengths, checked in every build), `mem::zero(xs)` and `mem::zero(&x)` (the size comes from the type), `mem::fill(xs, v)`, `mem::equal(a, b)` (lengths first), `mem::bytes(&x)` and `mem::read_bytes(&x)` (a value's bytes as a slice with its size), and `mem::can_view(raw, T)` with `mem::view(raw, T)`, the one place bytes become a typed slice (a failed check unless the bytes are a whole number of `T` and aligned).

**`smith-mem` is a clang-tidy check** (`shim/mem.h`, registered in `shim/tidy.cpp`, on in smith's default `.clang-tidy`; the editor and `smith lint` run it) that reports `memcpy`, `memmove`, `memset` and `memcmp` (`c::` and `__builtin_`, and the fortified `__builtin___memcpy_chk` the SDK's macro makes) in a `.smith` file with a `module` line, naming the replacement; `smith lint --fix` rewrites `memset(p, 0, sizeof *p)` and `memset(&x, 0, sizeof x)` to `mem::zero`. The marker that lets a wrapper call the raw function is the one the prover already reads, `contract::trusted()` (`smith_trusted()`): the check skips a function that calls it. The list is a table in `shim/mem.h`, a row for each wrapped C function, so later wrappers (files, strings, `qsort`) join by a row once their typed replacement exists. A `.c` file and a `.smith` file with no `module` line are untouched (the C rule).

Limits today: a generic with two `auto` slices of one type reads them as `.data` and `.count[0]` (#408), `mem::view` is a checked cast with `can_view` beside it because an `option` of a slice cannot be a generic's result yet (#408), and the libraries smith links itself (`io`, `inflate`) and a few files the lowering still breaks on (#421) keep their raw calls under `// NOLINTBEGIN(smith-mem)` until smith has been released with `smith::mem`.
