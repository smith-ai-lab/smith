---
type: architecture
title: Effect inference, a plugin linked into smith
description: "Effects are inferred for every function a compile reads, from its body and its callees, and [[std::pure]] and one attribute for each effect are ceilings checked by one more clang plugin linked into smith."
tags: [language, plugin, effects]
---
# Effect inference (issue #207, first step)

What a function does to the world, as a set of effects (`alloc`, `files`, `net`, `console`, `clock`, `process`, `env`,
`entropy`, `globals`, `c`, `fails`: one bit each, `shim/effects_table.h`), computed by `shim/effects_infer.cpp`, a second plugin of the
kind [affine-types](affine-types.md) is (an AddAfterMainAction that reads the translation unit when it is complete, and
a ParsedAttrInfo for the attributes; `smith_effects_disable()` turns it off in clang-tidy, which cannot report a
diagnostic it has no id for). Skill `smith-lang-effects`.

- **Inference.** Each function with a body gets the union of the effects of its calls, to a fixed point through the
  functions of the file and the headers it includes. A call the file cannot see into is a leaf: a function of libc or
  libSystem has the set `effects_table.h` gives it (pure ones, `malloc` is `alloc`, `read` is `files` and `net`, `printf` is
  `console`...), a declaration with an attribute and no body has the attribute's set (a promise), any other is `c`, as is a
  call through a function pointer, inline assembly and an Objective-C message. Writing a global or a function-local
  static is `globals`. Failing is the effect `fails` (issue #325, owner: effects are never a bad thing, what matters is knowing and controlling them): a call of `abort`, `__builtin_trap` or `smith_failure*` (the table has them as `fails`) is a failure point, which a checked slice index, a contract and an arena budget lower to, and every caller inherits it, in the file and across files through the summaries. A check that `smith prove` proved adds no `fails` in any build (the lowering reads `.smith/proved/<source>.pts`; an optimized build leaves it out, debug keeps it under a name that is no failure point). `smith prove`'s own compile does not judge `fails` ceilings (`SMITH_PROVING`): it must keep every check as a point to prove, so a ceiling would reject the function before it could be proved; nothing escapes, because every build's compile judges the ceiling after the proofs are known, with the proved checks no longer failure points.
- **Annotations are ceilings.** `[[std::pure]]` is the empty set and `[[std::effects(alloc, net)]]` lists what the
  function may do (the same spelling in C, in a module and on a declaration with no source, where it is a promise). It
  reaches the plugin's handler with its arguments through LLVM's `patches/clang-plugin-attr-args.patch` (r9): clang's
  parser read the arguments of a scoped C23 attribute only for an attribute it knew without the plugins, and a bare
  identifier after the first as an expression. Still accepted and not taught: one attribute for each effect
  (`[[std::alloc, std::files]]`) and `__attribute__((smith_effects("alloc", "net")))`; tests/effects/effects_internal.c.
  The declared set, not the inferred one, is what a caller sees.
- **The error** is at the first call that adds an effect beyond the ceiling and names the chain, `bad -> log_it (log.c:12) ->
  printf`; it is reported wherever a function carries an attribute (module, `.smith` without a module line, C) and in
  the editor.
- **Across files** (207b). Every compile that writes an object (bitcode under `-flto`, ThinLTO's release profile included) leaves a summary of it (`.smith/effects/<the object's path>.fx` in a project, `<object>.fx` beside it otherwise; the plugin, written whole and
  renamed): a line for each function (its linkage name, its declared set or -1, what its own body reaches, where) and for
  each thing it calls (a leaf with its effects, or a call by linkage name). The link of objects that have one, in
  `smith cc` (`smith_check_effects`, smith.c, so `smith build` too, `smith cc --smith-effects x.fx...` by hand; the place is `smith_effects_summary_path` in smith.c and the same rule in runtime/build_runtime.c, which declares it an output of each compile smith's own clang makes and an input of the link, so a summary that changed reruns the check),
  merges them, solves the sets again with the calls that cross files and reports a function whose ceiling is passed only
  through another file, with the chain; a symbol no summary defines is `c`. Why at the link and not at the compile: the
  compiles of a build are parallel and a callee's set may not exist when its caller compiles, so a compile-time read would
  differ between a clean and an incremental build; a module's header already carries the declared sets (its exports'
  attributes), which the importer treats as promises. A project function with no body in the file is *unresolved* at
  compile time (no effect, no error) where a system function the table does not know is `c`.
- **No recursion in a module** (207c): `checkRecursion` finds the strongly connected components of the call graph of the
  functions the module's file defines (a work list, no recursion) and reports a call that sits on a cycle, once for each
  cycle, with the way round (`a -> b (a.smith:4) -> a (b.smith:9)`); a call of `[[clang::musttail]] return f(...)` (an
  AttributedStmt with MustTailAttr over a return of the call) is a jump and does not count, so a cycle of jumps is a
  loop. Imports are acyclic, so a cycle is always inside one file; calls through a pointer and into code with no body are
  not followed. Only a file with a `module` line (it begins a line), so plain `.smith` and C say nothing. A survey on
  scratch copies of the three test beds (a name-based call-graph scan) found no cycle in any module; learner's C tool
  `tools/fetch/h5strings.c` has two tree walkers that recurse, outside modules. Tests: `tests/effects/recursion*`.
- **Not yet** (issue #207, later steps): the stack size, hover, and `effects::of` and `effects::within`. The capability
  check [effects-check](effects-check.md) stays until the owner's migration (#208).
