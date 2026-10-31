---
type: contract
title: Ownership idioms, what a returned pointer means
description: A returned pointer follows one of C's four idioms, written as [[std::returns_owned(f)]], returns_borrowed(n) or returns_internal; smith accepts, validates and records them, and judges no call site yet.
tags: [effects, ownership, attributes]
---
# Ownership idioms, what a returned pointer means

**A returned pointer follows one of C's four idioms** (owner, 2026-10-28, #720): owned with its free function
(`strdup`, freed by `free`), borrowed from an argument (`strchr` points into argument 1), the function's internal
memory (`strerror`, never freed by the caller), or the caller's context (the current allocator, the default for smith
code, so nothing is written). smith infers an idiom where it can read the code and takes a declaration where it can't.

- **Written** like `[[smith::effects]]`, in `shim/effects_infer.cpp`: `[[std::returns_owned(free)]]`,
  `[[std::returns_borrowed(1)]]` (1-based) and `[[std::returns_internal]]`, spelled `smith_returns_owned(free)` as a GNU
  attribute. In a module, name the C function as C does (`free`, not `c::free`).
- **Where**: on a function declaration, with or without a body; on a function pointer's typedef (put the attribute
  first: `[[std::returns_internal]] typedef char *(*Describe)(int);`, since clang rejects an attribute after
  `typedef`); and on a function pointer type itself as `[[clang::annotate_type("std::owns:internal")]]` after
  the parameter list, which clang keeps through typedefs as it keeps effects; a field or a variable of a function pointer type takes the attribute too.
- **Accepted**: `returns_owned(f)` names a declared function with one pointer parameter; `returns_borrowed(n)` has
  `n` a parameter that is a pointer or a slice; each applies only to a function returning a pointer or a slice; at
  most one per declaration (and one idiom for all declarations of a function, and for a function pointer type). Anything else is `error: smith-ownership: ...` naming the rule, on the type form too.
- **Recorded** in the object's `.fx` summary, beside the effects: `O <symbol> <idiom> <place>` for a function and
  `T <name> <idiom> <place>` for a typedef, idiom `owned:<f>`, `borrowed:<n>` or `internal`. A reader that does not know
  the tags skips the lines and a summary without them reads as before, so the format needed no version.
- **Not yet**: no inference, no table entries (`shim/effects_table.h` still lists `strdup` as `alloc` only), no check
  at a call site, no use by linear types (#438) or regions (#439). An unannotated function means what it did.

Skill: smith-lang-effects. Effects the same way: [effect-inference](effect-inference.md).
