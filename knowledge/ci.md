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

**The `mechanical` job** runs `fmt` and `docs` (under a second), then `lint` (about 105 s) and `prove` (about 130 s) at the same time instead of one after the other (#1139): both draw from the one `SMITH_CORES` pool, so they share the runner's cores rather than oversubscribe them. Each stage's output goes to its own log and is printed after both end (the last 60 lines of a failed one, then `FAIL <stage>`), and the job summary has a line `took <stage> <n>s (exit <code>)` for each stage. A job that was one hosted macOS job stays one (the free cap is 5 hosted macOS jobs at once across the organization, and a pull request run already uses 3 or 4).

**Where a failure shows.** A failing job does not cancel the run (#1139): it records as `failure`, and the sibling shards
run to completion, so one push shows every failure. Only a new push to a pull request cancels (the concurrency group).
`ci` waits for all jobs and is red when any is `failure` or `cancelled`, or when a job its scope owes (see the
Verdict step) did not succeed, a skipped one included; a job skipped because `build` failed is therefore red too. The
failing shard names its tests: the last lines of its log (the `Failed tests` step) and its job summary hold each failed
test's `FAIL <name>` line with the check and error lines under it (#1086); look there before re-running.
When the tests do not build, the step `Build the tests` of `setup-smith` ends with the compiler's
`file:line:col: error:` lines and the command that failed (also in the job summary, #1118). The errors go to stdout and
only the full output of the nested build is in `.smith/log/.smith_task_units-build.log` (a hidden file).

**Why each Linux shard builds smith itself** (measured on 2026-10-31 over 60 runs of each event, #1139). A shard spends
about 2.5 min before its first test: restore of the pack and step cache 0.5, `fetch` 0.2, `build` 1.2, `tests built` 1.1;
its tests then take 1.4 (a pull request's 4 `--changed` shards) to 2.4 min (the queue's 8). One `build-linux` job that
saved `build/` and `.smith/` for the shards to restore would put that 2.5 min, plus a save (about 0.5) and a restore
(0.3 to 0.5) of a build of about 1 GB, in series ahead of every shard: the verdict gets about 1 min later, and only the
runner minutes drop (about 17 of 41 per queue run). So the shards stay independent; the cut that does shorten the wall
clock is balance (`.github/shard-costs.tsv`, queue shard medians 3.5 to 5.7 min, about 0.5 min to gain) or a smith that
builds only the shard's own test programs (about 1 min).

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.

## Decisions

- Never merge a broken build (owner, 2026-10-25: "We should just have the discipline to not merge broken builds"): every change is a pull request from its own branch; GitHub's hosted runners run the full check on it (`ci.yml`), and GitHub's merge queue merges it, squashed, only when the required check `ci` passes on it together with main and the entries ahead of it; nobody pushes to main, and there are no exceptions for known failures (owner, 2026-10-29: "they make a pr when ready, those are running on github runners, not the self hosted one, once the ci passes, we merge into main").
