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
- **Not yet** (issue #207, later steps): effects across files (a module's header, a C file's summary: a call into another
  file is `c` today), the recursion ban and stack size, hover, and `effects::of` and `effects::within`. The capability
  check [effects-check](effects-check.md) stays until the owner's migration (#208).
