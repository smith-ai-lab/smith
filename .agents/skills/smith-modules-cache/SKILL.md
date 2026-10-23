---
name: smith-modules-cache
description: "Clang modules in a smith build: smith_modules in build.c, one module per header, the shared cache under ~/.cache/smith/modules. Load when a build.c calls smith_modules, when compiles are slow because headers parse again, or when smith_modules is called after a smith_object or twice."
---

# Clang modules: smith_modules

Clang modules parse a group of headers once, cache the result and reuse it wherever the headers are included again. The source stays plain C: an `#include` of a header in a module becomes an import. Apple's SDK already has maps for libc and every framework. The design is in [clang-modules](../../../knowledge/clang-modules.md).

```c
void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"src", "tests", nullptr}});
    // every smith_object after this one is compiled with modules
}
```

- **Opt-in.** smith never turns them on for you in a build.c; the conventions (no build file) do, with no folders, which still covers system headers and smith's own.
- **Which compiles:** every `smith_object` after the call whose source ends in `.c` or `.m`. The module flags go before the object's `.flags`, so a later `-fno-modules` wins. `.metal` and C++ sources never get them. Calling it after a `smith_object`, or twice, is an error.
- **`.folders`** lists header folders below the project (not `.`). Every `.h` under one, subfolders included, is a module of its own, named from its path (`src/base/math.h` is `src_base_math`). Folders may not overlap. smith's own headers (`.smith/include/smith/*.h`) always get a map, as modules `smith_arena`, `smith_io` and so on.
- **One module per header, never one per folder:** the depfile of a folder-wide module lists every header in the folder, so one edit would rebuild every source. With one per header, a rebuild is what the build already does without modules.
- **The cache** is `$XDG_CACHE_HOME/smith/modules` (else `~/.cache/smith/modules`), one for every project and worktree, so the SDK's modules are built once per machine. An entry unused for 31 days is pruned, checked once a week. `smith cache` shows it and `smith cache clean modules` empties it ([smith-packages](../smith-packages/SKILL.md)).
- `smith lsp` builds with the same flags from `compile_commands.json`, with no `--query-driver` ([smith-lsp](../smith-lsp/SKILL.md)).
