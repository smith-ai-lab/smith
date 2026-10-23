---
name: smith-dev-bootstrap
description: "The bootstrap lowering in bootstrap/: listing a source, smith build bootstrap, check-bootstrap, regenerating after a header or lowering change, and recovering when the build cannot start. Load when check-bootstrap or check-headers fails or after a merge that touches lib/smith or runtime/lang."
---

# smith-dev-bootstrap

Why it exists is in [smith-bootstrap](../../../knowledge/smith-bootstrap.md).

- **The bootstrap (`bootstrap/`).** A `.smith` source that smith's own build compiles is compiled from its committed
  lowering, `bootstrap/<path>.c` (`bootstrap/lib/smith/text.c`), with `config->cc`, so the stage0 and an older installed
  smith need no knowledge of the language and a fresh clone builds with `cc`. List the source in `build_bootstrap_sources`
  (build.c), run `smith build bootstrap`, commit the file, and have the build compile the `.c` (as `build_protocol` does
  for text). `check-bootstrap` (part of `check`) lowers again and fails naming the stale file, so change a `.smith` that
  is listed, run `smith build bootstrap`, commit both. `scripts/bootstrap.sh` writes the file: an include of the module's header, then `smith cc --smith-lower` (no compiler, so the stage0
  writes the same bytes) less what the header says; smith's runtime (`runtime/package.c`) includes `bootstrap/lib/smith/contract.c`, `arena.c`, `io.c` and `random.c` (and `lib/trace.c`, which arena's stop and contract call), embedded in smith.c and written to `.smith/bootstrap/lib/smith/`, because `io` needs the others, and so does `tests/mcp_fake.c`; the executor's growing buffer is its own (`SmithGrow` in `runtime/executor.c`), since io's is private; a test that reaches a module's private names (`tests/random.c`) includes the lowering; it has no comments, but keeps `#include`, `#define`, `#pragma`, `#error` and `#if`
  chains as written (every branch is lowered; a `#define` body with `::` or a sized type is an error). The lowering has no parser, so a generic call in a listed module writes its type (`arena::push_array(arena, f64, count)`) and calls no `auto` function, and uses no `if constexpr` that asks about a type; it does read a module it imports, so `arena::Arena` is `SmithArena`. `tests/bootstrap.sh`
  (`smith build test-bootstrap`) checks the files, the stage0's bytes, the lowering in an empty environment, and the C under
  `/usr/bin/clang` and `cc`. knowledge/smith-bootstrap.md.
- **Regenerate after a merge that changes a library module or the lowering.** When `lib/smith/*.smith` or `runtime/lang*.c` changed on either side, run `smith build headers` and `smith build bootstrap`; `check-headers` and `check-bootstrap` fail until it is done. A merge of docs or skills alone needs neither.
- **When `smith build headers` or `bootstrap` cannot build smith** (it embeds the stale bootstrap file after a header change), regenerate that module's bootstrap by hand, `sh scripts/bootstrap.sh <smith> lib/smith/<name>.smith bootstrap/lib/smith/<name>.c`, then build.
- **A change to a library's header or its smith.c row in the same commit:** the branch's smith embeds the old header, so rebuild it with the installed smith (`smith build`), then regenerate (`build/<OS>-<arch>/smith build headers`, `... bootstrap`), then build again.
