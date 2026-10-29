---
type: design
title: CI on GitHub's hosted runners, sharded
description: Every pull request and merge queue entry runs the full check on hosted macOS runners, split by test file across parallel jobs and gathered by one required check, ci; a push to main publishes the release.
tags: [ci, process]
---
# CI on GitHub's hosted runners, sharded

`.github/workflows/ci.yml` (#798) runs on `pull_request`, `merge_group` and `push` to `main`, on hosted runners only (`macos-26` and `ubuntu-latest`), never self-hosted; how a change merges is [ci](ci.md). A push to `main` is a commit the merge queue already checked: it runs only the `release` and `release-linux` jobs, which publish the stage0 release for each platform ([release](release.md)), and `install`. The Linux test parts join by themselves once a Linux release exists (#799): without one, stage0 builds smith from source for hours.

**Setup of a job** (the composite action `.github/actions/setup-smith`): `scripts/stage0.c` fetches the stage0 release of the newest ancestor commit ([smith-bootstrap](smith-bootstrap.md)), its `smith build llvm` downloads the LLVM pack of this tree's identity from its release instead of building it (#797), and the stage0's `smith build` builds smith, whose path is `$SMITH`. A job's machine is discarded, so a cancelled or timed-out job leaves nothing behind (#792).

**The jobs.** `changes` decides the scope; `mechanical` runs `fmt --dry-run --Werror`, `docs`, `lint` and `prove` (each reported, every one run, a failure ends in `FAIL <stage>`); `test` is a matrix of 5 macOS parts, plus 2 Linux parts (`ubuntu-latest`, `--shard i/2`) when `changes` finds that an ancestor's release has `smith-linux-x86_64.tar.xz` (the release stage0 would use; `changes` writes the matrix as its output and says which in the summary), each `smith test --shard i/n --costs .github/shard-costs.tsv`; `ci` gathers them and is the one check branch protection and the merge queue name (it fails when any job failed or was cancelled; a skipped job does not count against it). A failing test is named in the job summary and as an annotation (`FAIL <name>`).

**The split** is `--shard i/n` of the runner ([build-tests](build-tests.md)): a test file stays whole in one part, files go heaviest first to the lightest part, and the weight of a file is the CPU seconds `.github/shard-costs.tsv` gives its tests (0.4 for a test it does not name, so a new test needs no entry). The file holds the tests of 2 CPU seconds or more from a green run's `.smith/log/test.log` (the `check-log` artifact of a train run, or a `test-log-*` artifact of this workflow, which also runs with `SMITH_TEST_STATS=1`): refresh it from there when a part is much slower than the others. Raise the count `n` of the matrix to go from about 10 minutes to 5.

**Scope.** On a pull request or merge queue entry, the files changed against the base decide: only `LICENSE*`, `CONTRIBUTING.md` and root images (documents no build reads) run nothing and `ci` passes; only those and `README.md`, `AGENTS.md`, `knowledge/` and `.agents/` run `smith docs` and the README and skills tests (`--test readme_test,skills_test`); anything else, a push to `main` and a diff that cannot be read run everything.

**Not covered by a hosted run:** the debugger tests need a runner that may debug (they do nothing for an account that may not, [debugger](debugger.md)); the editor tests need `nvim`, which the macOS jobs install with Homebrew.
