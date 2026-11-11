---
type: design
title: CI on GitHub's hosted runners, one build and sharded tests
description: A pull request runs no check of its own (its one job ci passes at once; the checks are the pre-push hook and the merge queue); a merge queue entry runs the full suite: it builds smith once on a hosted runner, then runs the checks and the tests split by test file across parallel jobs that unpack that build; one required check, ci, gathers them and the first failure ends the run; a push to main publishes the release and saves the cache.
tags: [ci, process]
---
# CI on GitHub's hosted runners, one build and sharded tests

`.github/workflows/ci.yml` (#798) runs on `merge_group`, `push` to `main`, a nightly schedule and `workflow_dispatch` (by hand), on `ubuntu-latest` for Linux and, for macOS, on hosted `macos-26`, with the owner's Mac (three self-hosted runners, `[self-hosted, macOS, ARM64]`) only as overflow when the hosted pool is saturated (macOS picked per run; the Linux jobs when they are about to start, #2259);  how a change merges is [ci](ci.md). A pull request runs only `ci-pr.yml`'s one job `ci` (#3170); the pre-push hook is the check before the merge (#3132). A push to `main` is a commit the merge queue already checked: it runs `publish`, which publishes the stage0 release from the Linux tarball the queue run of that commit packed and the macOS tarball the push's `release` job builds ([release](release.md), #2832), `warm` (saves the cache, on macOS and on Linux), and the macOS tests (`macos`, `macos-lint`), which gate nothing. Linux carries the tests and the checks (fmt, docs, lint, prove) and is required (#73): the Mac runs only what macOS alone can.

The rest is one file per topic, so a change to one topic edits one file. A new CI fact goes in the file of its topic as its own paragraph.

- [CI jobs, scope and the split](ci/jobs.md): What the ci.yml jobs are, how the scope of a run is decided, what a pull request runs and what the queue runs, how the tests are split into parts, the times, and what a hosted run does not cover.
- [CI and the merge queue](ci/queue.md): How a pull request's run yields to the merge queue, queue-ops, a group that left the queue cancelling itself, one queue run at a time with a group size that follows the queue's health, main's runs not cancelling each other, and a failure not ending the run.
- [CI caches, downloads and the step cache](ci/caches.md): What a job's setup restores and builds, the lint, proof and test-result caches, the step cache for cloud sessions, where packages come from, and downloads that retry.
- [The front-end coverage count in CI](ci/count.md): The front-end coverage count, counted after every code merge to main and by hand, from nothing.
- [The owner's Mac in CI](ci/mac.md): GitHub's runners come first and the owner's Mac is overflow, the Mac's Linux runners, the owner's setup steps and the fetch-smith check on the Mac.
- [Testing a main-only step before it merges](ci/warm.md): How warm writes are tested as a dry run before a change merges, and one warm per OS at a time.
- [The runner ledger](ci/ledger.md): Where the 20 hosted job slots go: runner-minutes by event, job and outcome over a window, and how to take it again.
- [Where a pull request's hours go](ci/pr-life.md): Re-measuring a PR's life from claim to merge, the queue's removals, red checks by job, and file overlap, from REST records.
