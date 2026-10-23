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
