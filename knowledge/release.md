---
type: design
title: A stage0 release per merge to main
description: Every green merge to main publishes the GitHub Release stage0-<commit> with smith's tarball and checksum, and old ones are pruned.
tags: [ci, release, bootstrap]
---
# A stage0 release per merge to main

`smith build release` packages the smith just built and publishes it, so the latest main binary is always one download away and a fresh clone can bootstrap from the release of its newest ancestor commit that has one. Nothing in the repository records which release to use, so a release needs no commit.

**What is published.** A GitHub Release tagged `stage0-<full commit hash>` holding `smith-<os>-<arch>.tar.xz` (`bin/smith` and the links `smith-cc` and `smith-c++`, nothing else: the embedded files are inside the binary) and `smith-<os>-<arch>.tar.xz.sha256` (`shasum -a 256` format). The full smith is about 235 MB and the tarball about 67 MB (xz, 43 s CPU), far under the 300 MB limit set for this design.

**Where it runs.** Only in CI: the `release` job of `.github/workflows/check.yml`, on a push to `main`, `needs: install` (so after a green check and install), with `GH_TOKEN` the workflow's `GITHUB_TOKEN` and `permissions: contents: write` on that job alone. It is a job of its own so a failure of the release cannot fail the check or the install. It rebuilds with `smith build` (a cache hit after install) and runs `build/Darwin-arm64/smith build release`.

**The guard.** `scripts/release.sh` (the task's one command) refuses by hand unless `HEAD` is on `main` and equals `origin/main`; in CI it accepts only `GITHUB_REF=refs/heads/main` with `GITHUB_SHA` equal to `HEAD`. It also refuses a binary whose `smith version` does not name `HEAD`. A tag that already exists is not created again, so a re-run is harmless.

**Pruning.** After publishing, the script lists the `stage0-*` releases and deletes (with `--cleanup-tag`) all beyond the newest 20 except the newest of each ISO week (Monday to Sunday). Other releases and tags are never touched. A failed prune is reported and leaves old releases; it never fails the release. The rule is `release.sh prune`, a function of "tag date" lines.

**Download side.** `scripts/release.sh fetch <tag> <dir>` runs `gh release download`, checks the checksum and extracts `bin/` into `<dir>`.

**Tests.** `tests/release.sh` (task `test-release`) uses no network and no real repository: the package of the smith under test, the pruning rule on fake tags, the guard in scratch git repositories, and publish and fetch against a stub `gh` that keeps releases in a folder. The release step itself runs only on a push to main, so its first real run is the first merge after this change.
