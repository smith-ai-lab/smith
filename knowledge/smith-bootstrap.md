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

**A clone with no smith** downloads the newest release and builds with it. The release is `stage0-<commit>` of main's newest
published commit: `scripts/release.sh tags` lists the tags, and `scripts/release.sh asset <tag> smith-<os>-<arch>.tar.xz <out>` downloads the tarball (and its `.sha256`) through the GitHub API's asset endpoint (no `gh`, no GraphQL, #829; the plain download URL needs a login, #1757). Check the checksum, unpack it, put its `bin/` first on `PATH` (so its `smith-cc` is the `CC` of the
build), then `smith build llvm`, which puts the published LLVM pack into `~/.cache/smith/llvm/<name>` when it is not there (about 1.6 GB,
minutes) and then `smith build`. The README's Install section has the commands. CI does the same in `.github/actions/fetch-smith`, which reads the
newest tags from the API's releases list (`scripts/release.sh tags`, #1757) and takes the first with a tarball for the platform. Nothing in the repository records which release to
use, and there is no C or shell bootstrap to maintain. Nothing in a clone newer than the release fails to build with it, because of the lag rule below.

**How the build uses it.** `CC` defaults to `smith-cc`. `build.c` compiles `lib/std/{arena,contract,inflate,io,random,text}.smith`
to objects with it and links them beside `smith.c`, which (and `runtime/package.c` in it) sees only their generated headers
in `.smith/include/smith/` (written by the previous release during the build, never committed). The build program of a user project (`.smith/runtime.c` and `package.c`) links the same five
libraries, compiled by `smith build` from the copies it writes to `.smith/lib/std/` with the smith itself, even when
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

**On Linux** a release has a Linux build, `smith-linux-<arch>.tar.xz`, which the Linux CI job publishes, and it is downloaded the same way.
