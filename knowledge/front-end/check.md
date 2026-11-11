---
type: architecture
title: "Front end: The checker: roots and reach"
description: "The The checker: roots and reach part of smith's own front end: The checker."
tags: [front-end]
---
# The checker

## Roots and reach (#2993: the rule; built by #2998 to #3001 and #3005)

Owner, 2026-11-09: "We always compile the absolute minimum amount." The design, its measurements and its prediction are in #2993; the rule is:

- **Roots.** Every command names where it starts: `main` for `smith run` and `smith build` of a program, the chosen tests for `smith test`, the `pub` declarations for a library. Every `@c_name` export, and every `pub` declaration of a module whose generated header a C file includes, is a root too, since the front end cannot see what C calls.
- **Reach.** Reading an imported file finds its top-level declarations (name, kind, signature) and skips each body by its indentation. A declaration's types are resolved the first time a use finds it; its body is parsed and checked only when a root reaches it through a call, a method call, `&f`, a function used as a value or a type. A generic body is checked once for each instance that is reached, and its error is shown with the call that made the instance.
- **Unreached code is not parsed, not checked and not emitted**, so a syntax, name or type error inside an unreached body cannot stop a run. An error in a file's top-level structure or in a signature still does.
- **The default checks everything that can run.** `smith check` and CI start from every program's `main`, every test and every `pub` declaration of a library. What none of those reaches is dead code: nothing can call it, so it can break nothing that runs.
- **`smith check --unreached` adds the dead code.** Owner, 2026-11-09: "You are checking everything, but you're only checking the things that you can actually call. So this is specifically for like dead code maybe even." The flag also reports each declaration of the project nothing reaches (`smith-unreached`) and each generic with no instance (`smith-no-instance`), checks the bodies of that code for errors, and fails when it finds any. It is opted into; nothing schedules it.
- **Today**, until those tickets land: the parser reads every body, the checker declares every signature of the import closure (`declare_imported`) and checks every body of the compiled file and none of an imported one; only generics are per use (`check_instances`). The file grain (a test selection builds only its test files, #2998) holds for both toolchains; the grain of a declaration is the new front end's alone.
- A function of an import that the checker cannot type is left out of the import's names (`declare_imported`), so its use says "has no public". An exported generic of today's syntax whose result names its own type parameter, `export T *push(Pool *p, constexpr Type T)`, is declared: `declare_function` leaves such a result untyped (`result_names_type_parameter`, #3101), as it leaves the `Type` parameter, and the call site says what it is. Before, the result `T *` was an unknown name and the function vanished, so an importer's `m.push(b, i32)` was "`m` has no public `push`" and the fixer never added `mut` to `b`. A `constexpr Usize n` parameter of today's syntax is still "not yet typed" and leaves its function out. Tested by `tests/front_types_generic_result_test.smith`.
