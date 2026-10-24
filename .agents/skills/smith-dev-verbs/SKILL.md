---
name: smith-dev-verbs
description: "Adding or changing a top-level verb of smith: the verb table, bare arguments, the smith-cc links by argv[0] and the build program's compiler. Load before editing smith_verbs, runtime/verbs.c or the compiler links."
---

# smith-dev-verbs

What each verb means is in [verbs](../../../knowledge/verbs.md).

- `smith.c` ends with the verb table, `smith_verbs`: one row per top-level verb, a kind that says how it runs (`smith_dispatch`) and, for a project verb, the function that does it (`runtime/verbs.c`, included by `smith.c`). `SmithVerb_ToolOrProject` (`fmt`, `lint`, `prove`) is the tool with arguments and the project action with none; `SmithVerb_Project` (`run`, `test`) and `SmithVerb_ProjectBare` (`check`) are the action alone. `smith docs` (`SmithVerb_Docs`, `runtime/docs.c`) is always the check, with the types of `knowledge/index.md`. The actions read `.smith/build.info` (written by every run of build.c, `runtime/build_runtime.c`: `smith_put_build_info`) and build the manifest's `smith_needs` or `smith_tests`; `smith build <verb>` is refused for the seven fixed names (`smith_is_project_verb`), and `smith_task` and `smith_alias` refuse them (`smith_verb_names` in the runtime). A new verb is a row and a function.
- A bare argument (`smith foo.c`) is `smith_bare_arguments`: an error that says what to run. Do not route it to the compiler.
- `smith-cc` and `smith-c++` are links that `main` dispatches by `argv[0]`, as are `clang` and `clang++`, the names of the programs folder's links, which the driver runs again with `-cc1`; `ld` is the linker. A test that copies smith under another name and expects a compile must pass `cc`.
- `smith build`'s manifest of the build program (`.smith/build-program/build.ninja`) compiles `build.c` with `<smith> cc ...` when clang is inside smith, and with the external clang itself otherwise. `build.c` files name `smith-cc` and `smith-c++` as compilers; `"smith"` is an error in `smith_object` and `smith_program`.
