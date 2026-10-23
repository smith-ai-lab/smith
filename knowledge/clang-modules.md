---
type: design
title: Clang modules, smith_modules
description: Clang modules are implicit, one per header, opt-in, and cached once per machine.
tags: [modules, cache]
---
# Clang modules, smith_modules

**Clang modules are implicit, one module per header, and opt-in.** `smith_modules` in build.c writes the maps under `.smith/modules/`; clang keeps the cache in `$XDG_CACHE_HOME/smith/modules` (else `~/.cache/smith/modules`), one for every project and worktree, so the SDK's modules are built once per machine. One module per header (not an `umbrella` directory), because the depfile of an umbrella module lists every header in the folder and an edit would rebuild every source. compile_commands.json keeps the module flags (`smith lsp` builds them, with no `--query-driver`). the conventions turn them on; [the build file](build-file.md) has the design.

Clang modules parse a group of headers once, cache the result, and reuse it wherever the headers are included again. The
source stays plain C: every `#include` of a header in a module becomes an import. Apple's SDK ships module maps for libc
and every framework, so `#include <stdio.h>` and `#import <AppKit/AppKit.h>` already have modules; `smith_modules` adds
the project's own headers and smith's.

```c
void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"src", "tests", nullptr}});
    // ... every smith_object after this one is compiled with modules
}
```

- **Off until build.c says so.** smith never changes a default. The conventions turn it on (`smith_modules(b,
  &(SmithModules){})`, with no folders, which still covers system headers and smith's).
- **Which compiles.** Every `smith_object` after the call whose source ends in `.c` or `.m`: the flags are added before
  the object's `.flags`, so a later `-fno-modules` wins. `.metal` and C++ sources never get them. Calling it after a
  `smith_object`, or twice, is an error.
- **`.folders`** lists header folders below the project (not `.`). Every `.h` under a folder, subfolders included, is a
  module of its own, named from its path (`src/base/math.h` is `src_base_math`). The map for a folder is
  `.smith/modules/<folder with / as _>.modulemap`. Folders may not overlap. smith's own headers
  (`.smith/include/smith/*.h`) always get a map, `.smith/modules/smith.modulemap`, as modules `smith_arena`,
  `smith_io`, and so on.
- **One module per header, not one per folder.** clang's `umbrella` directory makes the whole folder a single module,
  and the `-MD` depfile of every source then lists every header of that folder, so editing one header rebuilds every
  source that includes any header there. With one module per header (build.c's `smith_glob` finds them, so nobody lists
  them) the depfile lists exactly the headers a source imports, directly and through other headers, and a rebuild is
  what the build already does without modules. Each module is `module <name> { header "<absolute path>" export * }`; `export
  *` makes a header's own includes visible to its users, as textual inclusion did.
- **The flags:** `-fmodules -fmodules-cache-path=<cache>/smith/modules -fmodules-prune-interval=604800
  -fmodules-prune-after=2678400` and one `-fmodule-map-file=<map>` per map. `<cache>` is `$XDG_CACHE_HOME`, or
  `~/.cache`, as for packages. The cache is clang's own (implicit modules), so clang does the locking when many compiles
  fill it at once. A sanitized compile and a plain one hash differently, so each has its own folder inside the cache.
  See "The caches" below for why it is shared and for the pruning flags.
- **Rebuilds.** The depfile lists the module maps and the headers imported, so editing a header rebuilds the sources that
  import it; editing or adding a header in a folder rewrites its map, which rebuilds the sources that load it. A flag
  that changes a module's hash (a `-D`, the standard, a sanitizer) changes the command, so the objects rebuild and
  clang builds the module again under the new hash.
- **compile_commands.json keeps the module flags**, so the editor sees what the build does. Use `smith lsp` (clangd) with no
  `--query-driver`: with it, clangd cannot build the SDK's modules (not even `#include <stdint.h>`: the SDK's map
  requires a feature that the extracted `-isystem` paths clear). Measured on game's `src/platform/macos.m` with its real
  module flags: clangd without `--query-driver` reports 0 errors, with it 1 (`module
  '_c_standard_library_obsolete' requires feature 'found_incompatible_headers__check_search_paths'`).
- **Isolation, and `.textual`.** A header is compiled alone when it is first imported, so one that works only after
  another include, or after a `#define` before it, fails there:

  ```
  While building module 'inc_needs_stddef' imported from bad.c:2:
  inc/needs_stddef.h:2:1: error: unknown type name 'size_t'
  ```

  The fix is for the header to include what it uses. A header that stays textual (it is included several times with
  different macros, or is not valid alone) is listed in `.textual`, spelled as `smith_glob` prints it
  (`"src/legacy.h"`): it is then not a module and every `#include` of it is textual, as before. A name that no folder
  holds is an error.
- **Measured** on a busy machine: Objective-C that parses AppKit, Metal and
  AVFoundation (game's `src/platform/macos.m`) compiles in 0.19 s instead of 0.67 s with a warm cache; filling the cache
  the first time costs 2 to 4 s and about 70 MB per configuration. Unity builds with a few translation units gain little,
  because the parse is not their critical path.
- **Explicit modules** (smith builds each `.pcm` as a step and passes `-fmodule-file=`) are not implemented.
  They would make the module graph part of the build graph: no cache locking, exact rebuilds of the module files, a
  reproducible build, and a depfile that needs no help. They cost build.c a model of the module dependencies (which
  module imports which), which clang's scanner (`clang-scan-deps`) can supply.
