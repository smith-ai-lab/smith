---
type: contract
title: Ownership idioms, what a returned pointer means
description: A returned pointer follows one of C's four idioms, inferred from the function's body and summarized across files like effects, or stated with [[std::returns_owned(f)]], returns_borrowed(n) or returns_internal; smith records them and judges no call site yet.
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
- **Inferred** (#725; `shim/effects_infer.cpp`, `OwnerReader` and `OwnSolver`). Every function with a body that returns a
  pointer or a slice gets an idiom from its `return` statements, the way effects come from its calls. Each return is a
  *path*: owned (the result of a libc table entry, of a function with an attribute, or of a function already owned, with
  its free function), borrowed from parameter `n` (the parameter, `&p->f`, `&p[i]`, `p + k`, a sub-slice, a local
  variable assigned from those, or what a borrowed-returning function returns for such an argument), internal (a static,
  a global or a `constexpr`, a string literal, an internal-returning callee), the caller's context (`mem::alloc*`,
  `arena::push*`, matched by the C name, or a callee already context), or a call whose callee another file defines. A null
  constant is every idiom and adds no path. The paths settle to one idiom with one solver, used by the compile (for the
  file's functions) and by the link (for every file's), so the two cannot disagree; a cycle of callees is an error.
- **Lifetimes merge to the shortest** (the ranking of #439: borrowed, then internal, then context): `borrowed:1` with
  `internal` is `borrowed:1`, and `internal` with `context` is `internal`, because a longer-lived path satisfies the
  shorter claim. Owned stands alone: owned with anything else, two free functions, or two parameters are
  `error: smith-ownership: 'f' returns different things on different paths: ...` naming the places and the attributes
  that fit. An origin smith cannot read (a pointer loaded from a field or through a pointer, from a function pointer, a
  local array, a variable whose address is taken) is `error: smith-ownership: 'f' returns a pointer whose origin smith
  cannot read (...); write [[std::returns_owned(f)]], ...`, never a guess. An attribute wins only when every path smith
  can read agrees with it (a longer-lived path is covered); a body that contradicts it is `error: smith-ownership: 'f' says
  [[std::returns_borrowed(1)]] but returns a pointer that is owned, ...`. The two refusals are for a module; a `.c` file
  is inferred for and never refused, except for an attribute its body contradicts. A struct returned by value has no idiom
  and no error (#725's proposal for a struct that holds pointers is not decided). A `contract::trusted()` function and an
  instance of a generic (the lowering cannot take an attribute on a generic) are inferred where they can be read and are
  never an error.
- **In the summary**: a settled idiom is an `O` line (the same form, no marker: a reader needs nothing to tell inferred
  from stated, and a function with an attribute and a body is stated by the attribute, which the paths agree with),
  and the caller's context is `O <symbol> context <place>`, written because a caller in another file cannot tell the
  default from "not read" without it. A function that waits on a callee another file defines writes
  `R <symbol> <place> <name> <flags> known <idiom> | call <callee> <arg idioms joined by |> <callee name> | unknown
  <reason>`, one per path (flags `m` for a module, `t` for trusted), and the link settles them from the `O` and `R`
  lines of all objects (`smith_effects_check`): a function that is still unread there is an error naming the callee,
  and `<program>.owns` lists the idioms only the link could settle. `smith build ownership [-Dproject=<folder>]` counts
  the functions per idiom over a project (the plugin writes one line a function to `SMITH_EFFECTS_OWNERSHIP`).
- **Not yet**: no check at a call site, no use by linear types (#438) or regions (#439), no attribute for the caller's
  context, none on a generic function, no editor diagnostics for what only the link settles.

Skill: smith-lang-ownership. Effects the same way: [effects-check](effects-check.md).
