---
type: architecture
title: Effect inference, a plugin linked into smith
description: "Effects are inferred for every function a compile reads, from its body and its callees, and [[smith::pure]] and one attribute for each effect are ceilings checked by one more clang plugin linked into smith."
tags: [language, plugin, effects]
---
# Effect inference (issue #207, first step)

What a function does to the world, as a set of effects (`alloc`, `files`, `net`, `console`, `clock`, `process`, `env`,
`entropy`, `globals`, `c`: one bit each, `shim/effects_table.h`), computed by `shim/effects_infer.cpp`, a second plugin of the
kind [affine-types](affine-types.md) is (an AddAfterMainAction that reads the translation unit when it is complete, and
a ParsedAttrInfo for the attributes; `smith_effects_disable()` turns it off in clang-tidy, which cannot report a
diagnostic it has no id for). Skill `smith-lang-effects`.

- **Inference.** Each function with a body gets the union of the effects of its calls, to a fixed point through the
  functions of the file and the headers it includes. A call the file cannot see into is a leaf: a function of libc or
  libSystem has the set `effects_table.h` gives it (pure ones, `malloc` is `alloc`, `read` is `files` and `net`, `printf` is
  `console`...), a declaration with an attribute and no body has the attribute's set (a promise), any other is `c`, as is a
  call through a function pointer, inline assembly and an Objective-C message. Writing a global or a function-local
  static is `globals`. Crashing (`abort`, a trap, a failed check, a slice index out of range) is no effect.
- **Annotations are ceilings.** `[[smith::pure]]` is the empty set; `[[smith::alloc]]`, `[[smith::files]]`... are one
  attribute for each effect and a function's set is their union; `__attribute__((smith_effects("alloc", "net")))` takes
  the names as strings. The C23 spelling `[[smith::effects(alloc)]]` cannot carry arguments: clang's parser reads the
  arguments of a scoped C23 attribute only when its `hasAttribute` knows it without the plugins (checked in
  `ParseCXX11AttributeArgs`), so the handler of a plugin's attribute sees none. The declared set, not the inferred one, is
  what a caller sees.
- **The error** is at the first call that adds an effect beyond the ceiling and names the chain, `bad -> log_it (log.c:12) ->
  printf`; it is reported wherever a function carries an attribute (module, `.smith` without a module line, C) and in
  the editor.
- **Across files** (207b). Every compile that writes an object leaves a summary of it (`.smith/effects/<the object's path>.fx` in a project, `<object>.fx` beside it otherwise; the plugin, written whole and
  renamed): a line for each function (its linkage name, its declared set or -1, what its own body reaches, where) and for
  each thing it calls (a leaf with its effects, or a call by linkage name). The link of objects that have one, in
  `smith cc` (`smith_check_effects`, smith.c, so `smith build` too, `smith cc --smith-effects x.fx...` by hand; the place is `smith_effects_summary_path` in smith.c and the same rule in runtime/build_runtime.c, which declares it an output of each compile smith's own clang makes and an input of the link, so a summary that changed reruns the check),
  merges them, solves the sets again with the calls that cross files and reports a function whose ceiling is passed only
  through another file, with the chain; a symbol no summary defines is `c`. Why at the link and not at the compile: the
  compiles of a build are parallel and a callee's set may not exist when its caller compiles, so a compile-time read would
  differ between a clean and an incremental build; a module's header already carries the declared sets (its exports'
  attributes), which the importer treats as promises. A project function with no body in the file is *unresolved* at
  compile time (no effect, no error) where a system function the table does not know is `c`.
- **Not yet** (issue #207, later steps): the recursion ban and stack size, hover, and `effects::of` and `effects::within`. The capability
  check [effects-check](effects-check.md) stays until the owner's migration (#208).
