---
type: design
title: The caches and smith cache
description: Modules and packages are cached under one rule, an entry unused for 31 days is removed, each root has a byte cap with least-recently-used eviction, and smith cache shows and cleans them.
tags: [cache]
---
# The caches and smith cache

**Three caches, one rule:** modules (`<cache>/smith/modules`), packages (`<cache>/smith/packages`) and lint results (`<cache>/smith/lint`, below), `<cache>` being
`$XDG_CACHE_HOME` or `~/.cache`. An entry unused for 31 days is removed, checked at most once a week: clang does it for
modules (smith passes `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400`), `smith_package` for packages
(a `<hex>.used` stamp per package and `prune.timestamp`). Both are keyed by hash, so a removed entry is fetched or built
again. `smith cache` shows each cache's path, entries and size (`-v` lists the entries and their last use);
`smith cache clean [modules|packages|lint]` empties them, and refuses while a `smith build` runs (builds hold a shared flock
on `<cache>/smith/lock`). Tests: `tests/cache_*_test.smith`, with caches of their own. See [the build file](build-file.md).

smith keeps two caches under `$XDG_CACHE_HOME/smith/` (or `~/.cache/smith/`), each keyed by hash, so anything removed
from either is simply fetched or built again:

- `packages/<hex>/`: what `smith_package` extracted (above).
- `steps/`: the executor's compile steps (`m/` manifests, `r/` results; [the executor](executor.md) says what is keyed).
  The 31-day rule and #618's cap (`SMITH_CACHE_MAX`, least recently used first, the same `smith_cache_cutoff` code) apply;
  a hit touches its entry when a day old, and the check runs at most once an hour (`steps/prune.timestamp`), at a build's start.
- `modules/<LC_UUID of the smith>/`: clang's module cache for `smith_modules` (below), one folder for each smith binary
  (issue #575): clang keys a module by its flags and paths, not by the compiler and resource folder that built it, so two
  smiths on one cache (CI's, a lane's, the installed one) would read each other's modules. The folders are pruned sooner than the
  31 days, on use (CI installs after every push to main, 20 or more smiths a day, so a month of folders would be
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

**A byte cap on every cache root (issue #618), beside the 31 days.** The 31 days are far too slow for a cache that
tests fill (the tests' module cache reached 337 GB in 47,329 context-hash folders), so each root, `modules` and
`packages`, also has a cap in bytes: 10 GB, or `SMITH_CACHE_MAX` (bytes, with a `k`, `M`, `G` or `T` suffix counting thousands,
as `smith cache` shows sizes; `smith cache` prints the cap). It is checked when a write can have crossed it, and the oldest-used go first:

- **Modules:** when a build opens the cache, at most every ten minutes (every build when `SMITH_CACHE_MAX` is set, which a
  test does; the stamp is `modules/.cap-checked`). When the root is over the cap: first the folders of other smiths, least
  recently used (their `.used`) first, never one a running smith holds (the pruning's exclusive lock without waiting) and
  never this smith's; if that is not enough, this smith's own module files (`.pcm`), least recently used first, as
  clang's pruning removes them (clang touches a module file whenever it uses it), never one used in the last hour. A
  module a later compile needs is built again.
- **Packages:** after a fetch, the least recently used (their `<hex>.used`) until the packages fit, never the one just
  fetched, each by the weekly pruning's safe removal (renamed away, the stamp read again, renamed back if a build touched
  it meanwhile).
- **How:** an entry's bytes are counted in steps of age (half an hour), so the oldest are found without a table of entries:
  the cutoff is the age from which everything goes so that the rest fits (`smith_cache_cutoff` in `runtime/package.c`).

**What keeps new context-hash folders from starting** (measured with `clang -module-file-info` on the folders of one smith):
clang hashes the language options, the target, the sysroot and resource directory, the optimization level and sanitizers and
every `-D` into a module's context, and not the warnings, the diagnostics' colours or `-I` paths; a scratch folder's path
is no part of it. The macros smith itself defines per source or run and no header reads (`SMITH_PROVED`, `SMITH_PROFILING`,
`SMITH_RECORD`) are given as `-fmodules-ignore-macro=`, so they start no folder. A module file's name carries the hash
of its module map's path, so a project's own and smith's modules (`.smith/modules/*.modulemap`) have one file for each
project path in the folder; the cap bounds that growth.

**One rule for what is inside: an entry unused for 31 days is removed, checked at most once a week** (the module folders of smiths are the exception above: they go sooner). For modules smith states it
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

**The lint cache** (issue #621, `runtime/lint_cache.c`) keeps what clang-tidy said about one file so that `smith lint`
runs it only on a file whose lint could differ. It is in two levels, like a compiler cache with a manifest. The *first
key* is the SHA-256 of smith (build-id on Linux, LC_UUID on macOS: clang-tidy and smith's own checks are in the
binary), the environment that moves a compile (`SDKROOT`, `CPATH`, ...), the file's name, its compile command (the
root of the project replaced by a fixed word, so a clone elsewhere shares entries; a `.smith` file the build does not
compile has the whole `compile_commands.json` there) and the content of every `.clang-tidy` from its folder up to `/`; a `.smith` file's key also holds every generated header
under `.smith/include`, since its imports read those without the preprocessor and clang-tidy's dependency file never names
them (#1180).
Under it, `m/<xx>/<key>` is the *manifest* of the last run: the files clang-tidy read, which a miss collects through
`-Wp,-MMD` (so no build or object is needed, and a `.smith` file's imports are in it). The *second key* adds the
content of those files, and `r/<xx>/<key>` is the *result*: the exit status and the output with the root replaced. A hit
prints the stored output and returns the stored status; a miss runs clang-tidy as before and stores the result by
writing a temporary file and renaming it, so lints in other worktrees are safe together. Only a run that completed
(exit status 0 or 1) is stored, and not one that read a file changed during it or in the 20 ms before it. A system
header is not in a manifest (nor is a header found only because a new file shadows an earlier one in the include path),
so an SDK update is seen only through `SDKROOT`, `DEVELOPER_DIR` and smith's own identity. The rule is the other caches':
a hit touches its entries when a day old, and once a week (`lint/prune.timestamp`) entries unused for 31 days and
stale `t/` files go. `SMITH_LINT_CACHE=0` turns it off and `=why` prints why each file ran. `smith cache` counts the
files and `smith cache clean lint` removes them.
