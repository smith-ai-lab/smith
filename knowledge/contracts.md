---
type: architecture
title: Contracts are C23 attributes and calls that smith reads
description: Contracts are calls of smith::contract that smith prove reads from the IR, are not a clang plugin and do not depend on -fbounds-safety.
tags: [contracts, prove]
---
# Contracts are C23 attributes and calls that smith reads

**Contracts are C23 attributes that smith reads through libclang**, loaded with `dlopen` only when a check runs. Not a
clang plugin: a plugin's `[[smith::requires(...)]]` receives no arguments, and its `__attribute__` form cannot see the
parameters (tested on clang 22). An unknown attribute fails `-Werror` builds, so the spelling in `.c` files is open.

**Contracts do not depend on `-fbounds-safety`**, which only Apple's clang has. UBSan and the arenas' ASan poisoning
check bounds at run time; contracts will prove them.

**`contract::modifies` says what a function writes** (issue #54, agreed with the owner 2026-10-22): a call after the `requires`, `&x` for an object or a slice for a range (`xs`, `p[a:b]`; C: `smith_modifies(p, size)`), the empty `modifies()` for writes-nothing. A function without it keeps the old rule (it may write whatever its pointer parameters reach). The prover proves the body inside the ranges and callers keep everything else. Skill: smith-contract.

**Checks run in every build; only a proof removes one** (issue #326, owner, 2026-10-25; a proved `ensures`, `invariant` or `assert` is left out of an optimized build, a `requires` never, issue #325): `contract::requires`, `ensures`, `invariant`, `assert`, slice bounds, arena budgets and the library's preconditions check in debug, release and fast, `NDEBUG` or not. "Even in release builds, we probably want to keep preconditions, because if you have a broken invariant, that's a really big deal. But that's what proofs are for." There is no global switch and `target::assertions` no longer affects smith's checks. A hot spot is answered with more proofs; an explicit per-function opt-out would be its own decision, with numbers. Skill: smith-contract.
