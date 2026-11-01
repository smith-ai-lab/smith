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

**Every job has a `timeout-minutes`** set from the observed maximum with headroom (#1139): 3 for `changes`, `ci`, `publish`, `install` and llvm.yml's `pick`; 12 for `docs`; 15 for `build`, `mechanical`, `test`, `warm`, `release` and `release-linux`; 20 for `test-linux`; 240 for llvm.yml's `macos` and `linux` packs (observed 131 and 162). A job that grows past its limit raises the limit with the measurement.

**Where a failure shows.** A failing job does not cancel the run (#1139): it records as `failure`, and the sibling shards
run to completion, so one push shows every failure. Only a new push to a pull request cancels (the concurrency group).
`ci` waits for all jobs and is red when any is `failure` or `cancelled`, or when a job its scope owes (see the
Verdict step) did not succeed, a skipped one included; a job skipped because `build` failed is therefore red too. The
failing shard names its tests: the last lines of its log (the `Failed tests` step) and its job summary hold each failed
test's `FAIL <name>` line with the check and error lines under it (#1086); look there before re-running.
When the tests do not build, the step `Build the tests` of `setup-smith` ends with the compiler's
`file:line:col: error:` lines and the command that failed (also in the job summary, #1118). The errors go to stdout and
only the full output of the nested build is in `.smith/log/.smith_task_units-build.log` (a hidden file).

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.

## Decisions

- Never merge a broken build (owner, 2026-10-25: "We should just have the discipline to not merge broken builds"): every change is a pull request from its own branch; GitHub's hosted runners run the full check on it (`ci.yml`), and GitHub's merge queue merges it, squashed, only when the required check `ci` passes on it together with main and the entries ahead of it; nobody pushes to main, and there are no exceptions for known failures (owner, 2026-10-29: "they make a pr when ready, those are running on github runners, not the self hosted one, once the ci passes, we merge into main").
