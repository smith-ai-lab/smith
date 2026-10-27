---
type: design
title: The caches and smith cache
description: Modules and packages are cached under one rule, an entry unused for 31 days is removed, and smith cache shows and cleans them.
tags: [cache]
---
# The caches and smith cache

**Two caches, one rule:** modules (`<cache>/smith/modules`) and packages (`<cache>/smith/packages`), `<cache>` being
`$XDG_CACHE_HOME` or `~/.cache`. An entry unused for 31 days is removed, checked at most once a week: clang does it for
modules (smith passes `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400`), `smith_package` for packages
(a `<hex>.used` stamp per package and `prune.timestamp`). Both are keyed by hash, so a removed entry is fetched or built
again. `smith cache` shows each cache's path, entries and size (`-v` lists the entries and their last use);
`smith cache clean [modules|packages]` empties them, and refuses while a `smith build` runs (builds hold a shared flock
on `<cache>/smith/lock`). Tests: `tests/cache_*_test.smith`, with caches of their own. See [the build file](build-file.md).

smith keeps two caches under `$XDG_CACHE_HOME/smith/` (or `~/.cache/smith/`), each keyed by hash, so anything removed
from either is simply fetched or built again:

- `packages/<hex>/`: what `smith_package` extracted (above).
- `modules/<LC_UUID of the smith>/`: clang's module cache for `smith_modules` (below), one folder for each smith binary
  (issue #575): clang keys a module by its flags and paths, not by the compiler and resource folder that built it, so two
  smiths on one cache (CI's, a lane's, the installed one) would read each other's modules. The folders are pruned sooner than the
  31 days, on use (the train installs after every green merge, 20 or more smiths a day, so a month of folders would be
  gigabytes): each build that opens the cache stamps its folder (`.used`, touched when an hour old) and holds it with a
  shared lock on `.in-use` for the build's life, its compiles included; of the other folders the **3** most recently
  used stay (a lane's smith, CI's and the installed one swap in turn), and any other idle for **a day** goes, unless a
  running smith holds it (the pruner takes the exclusive lock without waiting, so a folder in use is never removed).
  It costs one `readdir` and a `stat` for each folder. Inside a folder clang prunes by the 31-day rule as below. It is
  one cache for every project and worktree, not one
  per project, so Apple's SDK and framework modules are built once per machine and a new clone or worktree starts warm.
  clang keys a module file by the module, its path and the hash of the compile's flags, so two worktrees whose headers
  differ (different paths) never share or overwrite one another's module files, and a sanitized compile never reuses a
  plain one's. A module file is checked against its headers when it is used and rebuilt if one changed, so a warm cache
  can only be stale in a way clang detects.

**One rule for both: an entry unused for 31 days is removed, checked at most once a week.** For modules smith states it
in every compile, as `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400`, and clang applies it (named
constants in `runtime/package.c`). For packages smith applies it:

- Each `smith_package` (and `smith fetch`) touches `<hex>.used` beside the package folder: that is its last use.
- The weekly check is due when `packages/prune.timestamp` is missing or older than a week; when it is not due, the cost
  is one `stat`. When due, the build touches the timestamp first (so a build starting meanwhile skips the check), then
  removes every package whose `<hex>.used` is older than 31 days. A package with no stamp (cached before pruning
  existed) is stamped, not removed. A `.tmp-*` name (a fetch in progress) is removed only when it is itself older than
  31 days. A stale stamp with no package is removed.
- **Safe beside a build that uses a package:** a build touches the stamp before it looks for the folder, and the prune
  renames a stale package out of the way (`.tmp-prune-*`) and reads the stamp again before it deletes anything. If the
  build's touch is there, the package is renamed back and kept; if the rename came first, the build finds no folder and
  fetches it again. So a build always gets a complete folder, and a package used in the last 31 days is never removed.
- Tests fake the clock through file times (`touch -t`) in `tests/cache_*_test.smith`, with caches of their own in a temporary
  `XDG_CACHE_HOME`.

`smith cache` prints each cache's path, number of entries and size on disk; `smith cache -v` also lists each entry and
its last use (a package's stamp; a module folder's newest file). `smith cache clean` removes both caches whole, and
`smith cache clean modules` or `smith cache clean packages` one of them. Like `smith build`, `smith new` and `smith
fetch`, `cache` is recognised only as the exact first argument.

**A build holds the caches.** `smith build` takes a shared `flock` on `<cache>/smith/lock` and keeps the descriptor open
in smith, which every job inherits, so the lock lasts as long as the build's processes (the system
drops it if smith is killed). `smith cache clean` takes the lock exclusively without waiting, and refuses with exit
status 2 while any build, in any project or worktree, holds it. That costs one `open` and one `flock` per build.
Showing the caches never takes the lock, and pruning does not need it: it is safe as above.
