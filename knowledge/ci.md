---
type: design
title: How a change reaches main
description: Every change is a pull request from its own branch; GitHub's hosted runners run the full check on it, and the merge queue merges it squashed once the required check ci passes on top of main.
tags: [ci, process]
---
# How a change reaches main

**One branch and one pull request per change.** An agent works on its own branch, pushes it and opens a pull request
(`gh pr create`, `Closes #N`). Locally it may run `smith fmt`, `smith lint`, `smith docs` and `smith test --changed`
for what it changed, never the full check; no git hook runs anything on a push, everything runs in CI (owner,
2026-10-29).

**CI is GitHub's.** `.github/workflows/ci.yml` runs the full check on the pull request on hosted runners
([ci-hosted](ci-hosted.md)); the one required check is `ci`. The macOS jobs run on hosted `macos-26`; the owner's Mac takes them only as overflow when the hosted pool is saturated (and never needs to be on).

**The merge queue merges.** When `ci` is green the pull request goes into GitHub's merge queue
(`gh pr merge N --auto`). The queue makes a group of up to 5 entries on top of main, runs `ci` on the group
(`merge_group`) and, when it passes, squashes each pull request into one commit on main; a red group is split and the
pull request at fault leaves the queue with its run linked. The ruleset on `main` requires a pull request, the check
`ci` and the queue, a linear history, and blocks force pushes: nobody pushes to main.

**After a merge** the push to main does not test again (the queue checked that commit); `ci.yml`'s `release` job
publishes the stage0 release `stage0-<commit>` ([release](release.md)). Then `install`, on the owner's Mac (its one
self-hosted runner, used for nothing else), downloads that release, checks it and puts it in `~/.local/bin`, so the
installed smith is always main's; nobody runs `smith build install`.

**Where a failure shows.** The first failing job cancels the run, so the other shards show as cancelled. The failing
shard names its tests: the last lines of its log (the `Fail fast` step) and its job summary hold each failed test's
`FAIL <name>` line with the check and error lines under it (#1086); look there before re-running.

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.

## Decisions

- Never merge a broken build (owner, 2026-10-25: "We should just have the discipline to not merge broken builds"): every change is a pull request from its own branch; GitHub's hosted runners run the full check on it (`ci.yml`), and GitHub's merge queue merges it, squashed, only when the required check `ci` passes on it together with main and the entries ahead of it; nobody pushes to main, and there are no exceptions for known failures (owner, 2026-10-29: "they make a pr when ready, those are running on github runners, not the self hosted one, once the ci passes, we merge into main").
