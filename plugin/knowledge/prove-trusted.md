---
type: concept
title: Trusted boundaries
description: contract.trusted() marks a function the prover assumes and never reads, for the edge of a program where the code is not C it can read; callers prove its requires and assume its ensures.
tags: [prove, trusted]
---
# Trusted boundaries

The prover reads C. At the edge of a program the code is not C it can read: the runtime layer, inline assembly, a libc function (`printf`), a library with no source in the build. A proved function that reaches one fails with `a call of printf, which has neither a body nor a contract`; the fix is `contract.trusted()` as the first statement of the function that makes the call (the message names it). `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `abs` and `__memcpy_chk` are known to the prover.

## Example

```smith
import std.contract
import c <stdio.h>

pub fn show(value: i32):
    contract.trusted()
    contract.requires(value >= 0)
    _ := stdio.printf("%d\n", value)
    contract.ensures(value >= 0)
```

## Rules

- **A trusted function is assumed, never proved.** Callers prove its `requires` at each call and assume its `ensures`; its body is not read. The report lists it as `trusted  show  (file:line)  not proved; callers prove its 1 smith_requires and assume its 1 smith_ensures` and counts it as `K trusted`, never proved. `--function=` of a trusted function is an error.
- **A caller may assume** it writes only through the pointer parameters it is given (not `const` ones), nothing else a proved function can see, and frees nothing. Its `ensures` says what it does to those objects. A function that writes a global the callers read is outside what a contract can state: keep that state out of the proved functions.
- **What it returns is a value, never poison**, so a caller may branch on it.
- **A trusted function with no contract** marks the edge and promises nothing; its result is arbitrary. A body-less declaration marked `[[clang::annotate("std::edge")]]` is the same.
- **Mark the smallest function that touches the edge**, give it the `ensures` its callers rely on, and prove everything else. The trusted contract is a claim you check by reading and testing.
- **A precondition that cannot be proved** is `requires` plus `trusted()`, with the reason in a comment.

## Interactions

- smith's own libraries (`arena`, `random`, `text`, `inflate`, `math`, `json`, `trace`) need nothing from you: a project's `smith prove` reads their sources only for their contracts, and never proves or lists their functions.
- A trusted check function with `contract.assert(holds)` and a trap gives a run-time check with a report ([contracts](contracts.md)).

## Replaces from C

Unchecked calls into libc and assembly inside code that is otherwise proved.
