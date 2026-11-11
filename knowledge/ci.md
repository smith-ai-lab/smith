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

**CI is GitHub's.** `.github/workflows/ci.yml` runs a light check on the pull request (the build, fmt/docs/prove/lint, the tests the change reaches, a build of smith on the Mac (its lint and tests only for Mac-only files, #2832); started by ci-dispatch.yml under a budget, never ahead of the queue, [ci-hosted](ci-hosted.md)) and the full suite in the merge queue, on hosted runners
([ci-hosted](ci-hosted.md), #2822): a green pull request is not a green queue run; the one required check is `ci`. The macOS jobs run on hosted `macos-26`; the owner's Mac takes them only as overflow when the hosted pool is saturated (and never needs to be on).

**The merge queue merges.** When `ci` is green the pull request goes into GitHub's merge queue
(`gh pr merge N --auto`). The queue makes a group of up to 5 entries on top of main, runs `ci` on the group
(`merge_group`) and, when it passes, squashes each pull request into one commit on main; a red group is split and the
pull request at fault leaves the queue with its run linked. The ruleset on `main` requires a pull request, the check
`ci` and the queue, a linear history, and blocks force pushes: nobody pushes to main.

**After a merge** the push to main does not test again (the queue checked that commit) and does not build either: the
queue run built the release of that very commit and kept the tarballs, and the push's `publish` job publishes them as
the stage0 release `stage0-<commit>` within minutes (#2832); `release` and `release-linux` build only a tarball the
queue run did not leave ([release](release.md)); `prune` deletes old releases after `publish`, off its path. Then `install`, on the owner's Mac (its one
self-hosted runner, used for nothing else), downloads that release, checks it and puts it in `~/.local/bin`, so the
installed smith is always main's; nobody runs `smith build install`.

**`changes` also fails a change that undoes a commit already on main** (#1473): `scripts/revert_check.c`, compiled with `cc` in a step of the job, takes the change's merge base and head and, for each of the last 50 first-parent commits of main and each file both touch, fails when the change removes at least 20 non-trivial lines that the commit added and at least 90% of them (a stale working tree squashed onto a moved main, as #1455 did; an ordinary rewrite stays below it). `Reverts #N` or `Rewrites #N` lets a deliberate revert or rewrite of the commit `(#N)` through (in a pull request run the line is read from the pull request body; in a merge group only from the queue entry's commit message, so put it in a commit message too): a rewrite is a later piece of a chain replacing what an earlier piece added (#1875 replaced #1855's lines; owner, 2026-11-03). `tests/revert_check_test.smith` tests it in scratch repositories. The check reads the body the run's event carried: editing the body afterwards and re-running the job still reads the old one, so put the line in the body before the push that runs CI (or push again after adding it).

**Every job has a `timeout-minutes`** set from the observed maximum with headroom (#1139): 3 for `changes` and `ci`, 10 for `publish` and `install`, 15 for `prune` and llvm.yml's `pick`; 15 for `warm`, `release` and `release-linux`, 60 for `release-pack-macos` (16 minutes observed, plus the wait for a macOS slot); 30 for `macos` (build and every macOS stage in one job, #1709); 20 for `test-linux`, `check-fmt-prove` and `check-lint`; 240 for llvm.yml's `macos` and `linux` packs (observed 131 and 162). A job that grows past its limit raises the limit with the measurement.

**One macOS job** (#1709). `macos` builds smith, then runs `lint` and the `--macos-only` tests one after another in that checkout They can't run at once: each verb runs the build in the checkout and two collide on `.smith/build.ninja` (`cannot replace .smith/build.ninja`). Each stage's output goes to its own log; a failed one prints its last 60 lines and `FAIL <stage>`, and the job summary has `took <stage> <n>s (exit <code>)` for each. It replaced five jobs that took four of the five hosted macOS slots per run, so runs queued behind each other and waiting jobs were cancelled at their time limits. `fmt`, `docs` and `prove` run on Linux, in `check-fmt-prove`, and `lint` runs there too, in `check-lint` (#2234): both restore the build of `build-linux` like a test shard, run beside the test parts, and run on pull requests as well, so a format or lint mistake fails the pull request and not the queue, where each failure ejects every entry behind it (#2137, #2171). `lint` stays on the Mac because each platform lints code the other can't see; a pull request whose diff reaches nothing only the Mac sees runs `macos` as a build of smith only (#2832: a macOS-only build error, a deprecation under `-Werror`, was first seen in the queue on #3014), and the queue's `macos` run is the final word for macOS-only findings.

**A documents-only diff builds nothing** (#3044). When every changed file is in `knowledge/`, `.agents/`, `.claude/skills/` or a Markdown file at the repository root (the one list is `gated` in the `Scope` step of `ci.yml`; Markdown elsewhere, such as `templates/new` or `tests/docs/fixtures`, is read by tests and stays code), `changes` gives scope `docs` and `mac_run=false`: no `macos`, no `watch`, no build, in the PR check and in the queue run alike. The one job `docs-linux` runs `smith docs` (0.02 s) with the published release, and, when the diff touches `README.md` or the skills, `readme_test` and `skills_test` (about 2 minutes cold: the test runner is built; 6 s warm). `ci` owes `docs-linux` for this scope. A push to main of only such files is scope `none`: no release, no warm build (nothing in the compiler changed). `tests/ci_scope_test.smith` runs the `Scope` script on a docs-only, a README, a code, a mixed, a template and a push diff, so the exception cannot silently widen.

**Where a failure shows.** A failing job does not cancel the run (#1139): it records as `failure`, and the sibling shards
run to completion, so one push shows every failure. Only a new push to a pull request cancels (the concurrency group).
`ci` waits for all jobs and is red when any is `failure` or `cancelled`, or when a job its scope owes (see the
Verdict step) did not succeed, a skipped one included; a job skipped because `build` failed is therefore red too. The
failing shard names its tests: the last lines of its log (the `Failed tests` step) and its job summary hold each failed
test's `FAIL <name>` line with the check and error lines under it (#1086); look there before re-running.
When the tests do not build, the step `Build the tests` of `setup-smith` ends with the compiler's
`file:line:col: error:` lines and the command that failed (also in the job summary, #1118). The errors go to stdout and
only the full output of the nested build is in `.smith/log/.smith_task_units-build.log` (a hidden file).

**Why one job builds smith for the Linux shards** (#1758). A shard used to spend about 2.5 of its 5.6 median minutes
before its first test (pack and step cache restore 0.5, `fetch` 0.2, `build` 1.2, `tests built` 1.1), measured on
2026-10-31 over 60 runs of each event (#1139). Then one `build-linux` job looked worse: the verdict got about 1 min
later and only runner minutes dropped (about 17 of 41 per queue run), so the shards built for themselves. On
2026-11-02 the trade-off flipped: the free plan's 20 hosted job slots were full most hours and runs waited 15 to 40 min
for a runner, so runner minutes are the limit (owner: no paid runners). Now `build-linux` builds smith and the tests
once, packs `build/` and `.smith/` with `tar --zstd`, and uploads it as the artifact `build-linux` (1 day); each shard
restores it (`setup-smith` with `artifact: build-linux`), gives the checkout's sources an old time so none is newer than
the build (the build's own files keep their times: touching them made every step run again, #2833), and runs its tests. Measurement before and after (3 queue runs, 3 pull request runs): in the PR of #1758.

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.

## Decisions

- Never merge a broken build (owner, 2026-10-25: "We should just have the discipline to not merge broken builds"): every change is a pull request from its own branch; GitHub's hosted runners run the full check on it (`ci.yml`), and GitHub's merge queue merges it, squashed, only when the required check `ci` passes on it together with main and the entries ahead of it; nobody pushes to main, and there are no exceptions for known failures (owner, 2026-10-29: "they make a pr when ready, those are running on github runners, not the self hosted one, once the ci passes, we merge into main").
- A queue entry that fails stays in the queue (the ruleset is `HEADGREEN`): the groups behind it are built on its tree and a later passing group merges it, so one hung job fails every group stacked on it (#3051: `frontend-coverage-shard (5)` sat 30 minutes in two runs, 45 minutes each, and #3048 queued behind both). Every step that can hang has its own timeout, far under its job's, and a retry when the failure is the timeout; the count shard's step gets 7 minutes and no retry (a hang from a diff is deterministic). A run that our own workflows cancel (`queue-guard`, `batch-wait`) writes why in its run summary. `queue-heal` (#3051) dequeues a front entry whose run is `cancelled` with its `ci` job failed (a timed-out job), unless `batch-wait` or `queue-guard` cancelled it; GitHub kept such an entry 58 minutes when only `failure` was read.
- `queue-heal`'s sixth pass (#3051) labels `ready` a PR whose newest merge_group run of the last hour failed and either its group's base run failed too, or its first failed job (a job that failed, or that fail-fast cancelled after a step of it failed) is the first failed job of another PR's run in that hour. The fifth pass then enqueues it when its `ci` is green. A head is labelled at most twice (the `ready` labels after its newest push); `dequeued` PRs are never touched. Of the 299 failed queue runs from 11-03 to 11-11, 200 belonged to PRs that merged later with no new commit (#3082).
