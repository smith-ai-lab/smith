---
type: design
title: smith is built by its previous release
description: A clone with no smith downloads the release of its newest ancestor commit and builds with it; no committed copy of the libraries, no stage0 built by Homebrew's clang.
tags: [bootstrap, release, language]
---
# smith is built by its previous release

smith's libraries are written in `.smith` and smith itself is C23 that links them, so building smith takes a smith: the
one the release job published for an earlier commit ([release](release.md)). The owner's decision (issue #112, which
replaced the committed C lowering in `bootstrap/`): "We want to be able to write Smith in idiomatic Smith, including all of
Smith's libraries", and "Getting rid of a second copy to maintain".

**A clone with no smith** builds and runs `scripts/stage0.c`: a plain C23 file (a `.c`: the lint gate rejects a module-less `.smith`; the C
rule keeps it the same as a `.smith`), so Apple's clang builds it (`mkdir -p .smith && clang -std=c23 -x c scripts/stage0.c -o .smith/stage0-tool &&
.smith/stage0-tool [arguments of smith build]`) and the bootstrap needs no shell. It fetches the tags, walks `git rev-list HEAD` for the first commit that
has a release `stage0-<commit>`, downloads it (`curl` over plain HTTPS from the release's public URL, a child process; no `gh`, no login, no GraphQL, #829), checks the SHA-256 (`scripts/sha256.h`, smith's own, on every platform), unpacks it with `tar` into `.smith/stage0/` and runs
`.smith/stage0/bin/smith build` with that `bin/` first on `PATH`, so its `smith-cc` is the `CC` of the build. Nothing in
the repository records which release to use. `~/.cache/smith/llvm` (`smith build llvm`, about 9 minutes once) must be there
already; `smith build` says so when it is not (the default target stops, `llvm` and `clean` still work).
`SMITH_STAGE0_RELEASE=<tag>` names another release. With a smith installed, `smith build` is all it takes, and CI does that.

**How the build uses it.** `CC` defaults to `smith-cc`. `build.c` compiles `lib/smith/{arena,contract,inflate,io,random,text}.smith`
to objects with it and links them beside `smith.c`, which (and `runtime/package.c` in it) sees only their generated headers
in `.smith/include/smith/` (written by the previous release during the build, never committed). The build program of a user project (`.smith/runtime.c` and `package.c`) links the same five
libraries, compiled by `smith build` from the copies it writes to `.smith/lib/smith/` with the smith itself, even when
`SMITH_CLANG` makes an external clang compile `build.c` (`.smith/build-program/build.ninja` is that manifest). smith has
no build with one `cc` command and no stage0 without clang inside any more.

**The lag rule.** Code compiled into smith itself (`smith.c`, `runtime/`, `build.c`, and the libraries it links: `io`,
`arena`, `contract`, `random`, `inflate`, `text`, `trace`) can use a language feature once the feature is on main, because
a release of that commit exists then and builds the next commits. A branch cannot use, in that code, the feature it is
adding itself: the release that builds it does not have it. Tests and libraries that are not linked into smith (`format`,
`math`, `autodiff`, `test`, `*_test.smith`) use a feature at once, since the smith under test compiles them. A change that
needs the feature in smith's own code lands in two steps: the feature, then, after its release, the use.
The same holds for the build API: `build.c` is compiled by the release smith with the `<smith/build.h>` that release embeds.

**What still uses the raw lowering** (`smith cc --smith-lower`, the token-level lowering with no parser): the command
itself and its corpus (`tests/smith-lang`), and the white-box tests that include a module's lowering to reach its static
functions (`tests/random.c`, `tests/prove/sound/smith_random.c` and `smith_arena.c`, written by their scripts at run time).
The header pass (`smith cc --smith-header`) is also parser-less until a later ticket moves headers to
the full compiler. Nothing is committed from the lowering any more.

**Why chicken and egg does not bite.** Deleting `bootstrap/` removed nothing a release needs to build the next one: a
release carries its own embedded runtime and headers, and the tree gives it `build.c`, `smith.c`, `runtime/`, `lib/` and
`include/`, which use only what that release compiles. A fresh clone's newest ancestor with a release is at least the
first release (`stage0-4f1d091`), which compiles every commit since because of the lag rule. A release is pruned after a
while ([release](release.md)): a clone older than the 20 newest releases (and a week each) pulls main first.

**On Linux** (issue #305) a release has a Linux build, `smith-linux-<arch>.tar.xz`, once the Linux CI job publishes it;
`stage0.c` takes it the same way. When no release can be had (no Linux build yet, the chain broken, or the download
failing) it builds smith from source instead: `scripts/seed_linux.c`, the seed (`SMITH_STAGE0_SEED=1` asks for it,
`=0` never builds it), which takes the published LLVM pack of the configuration (`llvm-23.1.3-r14-linux-x86_64`, fetched with `curl` and checked by `scripts/llvm_pack.c`) and builds the pinned LLVM with `scripts/llvm_linux.c` only when no pack exists (`llvm_pack fetch` exits 3; a pack that exists but fails to download stops the seed; `SMITH_SEED_FETCH=0` always builds), then stage A (`smith.c`, the shims
and LLVM, with weak stand-ins for the libraries made from their headers' `smith-names` and `smith-bodies` lines), the
libraries compiled by stage A, stage B linked with them, and stage B's `smith build`. It needs the system's C and C++
compiler (clang 19 or GCC 13 or later), lld (the link of smith), curl, tar and xz: with the pack that is minutes; without
one it also needs cmake, ninja, python3, curl and patch, and about two hours and 15 GB the first time.
