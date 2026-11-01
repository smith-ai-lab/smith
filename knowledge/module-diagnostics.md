---
type: contract
title: A module has no warnings
description: The compile of a .smith file with a module line fails on a fixed baseline of diagnostics, each with a written way to say I meant it; heuristic ones are opt-in and advisory.
tags: [build, diagnostics, modules]
---
# A module has no warnings

**In a module there are no warnings** (issue #414; owner, 2026-10-26: "Once you are a module, there are no warnings.
Every warning is an error"). A module is a `.smith` file with a `module` line; the build adds the baseline to every
compile of one, as `-Werror=<name>` after every other flag, so no `.flags` or `-Wno-` turns one off. A `.c` file and a
`.smith` file with no `module` line compile as before. Only the module's own code is judged: clang does not report
inside a system header.

**The rule for the baseline.** A diagnostic is in it only when C23 or Smith gives an honest way to say "I meant it", so
every error has a fix and no disable comment exists. Changing the list is a recorded decision (the table below).
Heuristics with no such form are extras: off unless `build::defaults` lists them in `.warnings` (shown by `smith lint`
and the editor, never failing a build) or `.errors` (failing it). Only the compile database carries `.warnings`, so a
build stays quiet.

| Diagnostic | Catches | Saying "I meant it" |
|---|---|---|
| `conversion`, `sign-conversion`, `shorten-64-to-32`, `float-conversion`, `implicit-int-float-conversion` | a lossy implicit conversion: `I64` to `I32`, `F64` to `F32`, float to integer, a sign change | a written cast (`checked::to`, #351) |
| `sign-compare` | `-1 < 0u` | make the types agree |
| `parentheses` | `if (x = y)`, `a & b == c` | extra parentheses |
| `unsequenced` | `i++ + i++` | separate statements |
| `implicit-fallthrough` | a case falling through | `[[fallthrough]];` |
| `return-type`, `return-stack-address`, `dangling` | a path with no return, a local's address returned | fix the code |
| `incompatible-pointer-types`, `int-conversion`, `cast-qual`, `cast-function-type-strict` | pointer and integer mixing, casting away `const` (explicit casts too), a function pointer cast to a type that differs at all (calling through it is undefined) | fix the code |
| `sizeof-pointer-memaccess`, `sizeof-array-argument` | `sizeof(p)` meant `sizeof(*p)` | fix the code |

Widening conversions that lose nothing and a constant that fits stay implicit.

**Not yet in the baseline** (each waits on something, and is an extra until then): `unused-variable`,
`unused-parameter`, `unused-but-set-variable`, `unused-result` and unused imports, which need `_` and `_name` (#413)
as the way to say "I meant it", and unused imports also a report from the lowering, since clang cannot see them;
`switch-enum`, which is a baseline error for an enum a smith module defines or imports and an extra for an enum from a
C header (naming every case breaks when a system adds one), a split no clang flag makes, so until the lowering or a
hook can tell them apart it is `.errors = {"switch-enum"}` for a project that wants it.

smith's own `lib/` is built with these flags. Code: `smith_module_baseline` in `runtime/build_runtime.c`.
