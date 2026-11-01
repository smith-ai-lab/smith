---
type: architecture
title: std::wrapping is the written way out of a module's overflow checks
description: Signed arithmetic that wraps in two's complement, by name, so a module can mean wrapping where the checked default would fail the task (issue #351).
tags: [libraries, arithmetic, checks]
---
# std::wrapping

Owner (2026-10-25): in a module, signed overflow, bad shifts, division by zero and out-of-range float conversion become checks in every build (issue #351); unsigned arithmetic wraps as C defines, and a program that means wrapping on a signed type says so. `std::wrapping` is that spelling.

**Decisions.**
- *Generic over the integer type:* `wrapping::add(a, b)`, `sub`, `mul` and `neg` return the type of `a`, lowered to `__builtin_*_overflow` with the result kept and the flag ignored, so the answer is the low bits in two's complement.
- *It works today and means the same after the checks land:* the checks change what the plain operator does in a module, not what `wrapping::` does.
- *No C spelling:* every export is generic.

**What waits.** The checks themselves (sanitizer flags for module files, the handler route, `fails` inference, proofs removing them), `wrapping::shl` and `wrapping::to`, and `std::checked` (`checked::add` returns `Option(T)`, which a generic cannot return yet; asked on #1084).
