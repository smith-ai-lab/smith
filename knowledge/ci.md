---
type: design
title: How a change reaches main
description: Every change is a pull request from its own branch; GitHub's hosted runners run the full check on it, and the merge queue merges it squashed once the required check ci passes on top of main.
tags: [ci, process]
---
# How a change reaches main

**One branch and one pull request per change.** An agent works on its own branch, pushes it and opens a pull request
(`gh pr create`, `Closes #N`). Locally it may run `smith fmt`, `smith lint`, `smith docs` and `smith test --changed`
for what it changed, never the full check; the pre-push hook (`.smith/hooks/pre-push`, written by `smith build`, #3132) runs `smith check --changed` (fmt, docs, lint, the tests the change reaches, prove) on a push, and everything else runs in the merge queue (owner,
2026-10-29).

**CI is GitHub's.** A pull request runs no check of its own (#3170): `.github/workflows/ci-pr.yml` has one job, `ci`, that succeeds at once so the pull request can enter the merge queue. The two checks are the pre-push hook on the runner's machine (`smith check --changed`, the same command as the queue's with the change as its selection, #3163) and the Linux suite in the merge queue (`.github/workflows/ci.yml`, on `merge_group`, on hosted runners; [ci-hosted](ci-hosted.md), #2822): a green pull request is not a green queue run; the one required check is `ci`. The macOS jobs run on a push to main and in the nightly run, never in a queue run, on hosted `macos-26`; the owner's Mac takes them only as overflow.

**The merge queue merges.** When `ci` is green the pull request goes into GitHub's merge queue
(`gh pr merge N --auto`). The queue makes one group at a time (one entry, #3286) on top of main, runs `ci` on the group
(`merge_group`) and, when it passes, squashes each pull request into one commit on main; a red group costs one run and
the next ones, at size 1, find the pull request at fault ([queue](ci/queue.md)). The ruleset on `main` requires a pull request, the check
`ci` and the queue, a linear history, and blocks force pushes: nobody pushes to main.

**After a merge** the push to main does not test again (the queue checked that commit). The queue run packed the Linux
release tarball of that commit; the push's `publish` job publishes it as the stage0 release `stage0-<commit>` within
minutes, with the macOS tarball its own `release` job builds (#2832). `release-linux` builds the Linux tarball only when
the queue left none ([release](release.md)). `publish` prunes old releases last, off its path. The push also runs the
macOS tests (`macos`, with the Mac's lint as a stage) and the Mac watchdog; they gate nothing: a failure is red on the push run, and the
lead files a ticket at the top of #1631.

**A `SessionStart` hook with matcher `compact` (`.claude/settings.json`, #3383)** runs `scripts/reload_role_skill.c`, which prints the whole `SKILL.md` of the role skill the session loaded (`smith-dev-lead` or `smith-dev-runner`, found in the transcript) back into context after a compaction, and nothing when it loaded neither; `tests/reload_role_skill_test.smith` tests it.

**`changes` also fails a change that undoes a commit already on main** (#1473): `scripts/revert_check.c`, compiled with `cc` in a step of the job, takes the change's merge base and head and, for each of the last 50 first-parent commits of main and each file both touch, fails when the change removes at least 20 non-trivial lines that the commit added and at least 90% of them (a stale working tree squashed onto a moved main, as #1455 did; an ordinary rewrite stays below it). `Reverts #N` or `Rewrites #N` lets a deliberate revert or rewrite of the commit `(#N)` through (in a pull request run the line is read from the pull request body; in a merge group only from the queue entry's commit message, so put it in a commit message too): a rewrite is a later piece of a chain replacing what an earlier piece added (#1875 replaced #1855's lines; owner, 2026-11-03). `tests/revert_check_test.smith` tests it in scratch repositories. The check reads the body the run's event carried: editing the body afterwards and re-running the job still reads the old one, so put the line in the body before the push that runs CI (or push again after adding it).

**Every job has a `timeout-minutes`** set from the observed maximum with headroom (#1139): 3 for `changes` and `ci`, 10 for `publish`, 15 for llvm.yml's `pick`; 15 for `warm`, `release` and `release-linux`; 30 for `macos` (#1709); 20 for `test-linux`, `check`; 240 for llvm.yml's `macos` and `linux` packs (observed 131 and 162). A job that grows past its limit raises the limit with the measurement.

**One macOS job** (#1709). `macos` builds smith, then runs the `--macos-only` tests one stage after another in that checkout: two verbs at once collide on `.smith/build.ninja` (`cannot replace .smith/build.ninja`). A failed stage prints its last 60 lines and `FAIL <stage>`, and the job summary has `took <stage> <n>s (exit <code>)` for each. `fmt`, `docs`, `prove` and `lint` run on Linux, in `check` (#2234), beside the test parts (#2137, #2171). `macos` lints too (last stage, #3568).

**A documents-only diff builds nothing** (#3044). When every changed file is in `knowledge/`, `.agents/`, `.claude/skills/` or a Markdown file at the repository root (the one list is `smith_plan_is_gated` in `runtime/check_plan.c`, which `smith check --plan` and the `Scope` step of `ci.yml` share (#3163); Markdown elsewhere, such as `templates/new` or `tests/docs/fixtures`, is read by tests and stays code), `changes` gives scope `docs` and `mac_run=false`: no `macos`, no `watch`, no test shards. A queue run still starts `build-linux` at once, without waiting for `changes` (#3277: a queue run always builds, and `pick-mac` is not run there; the fixed hosted values stand in its outputs). The one job `docs-linux` builds the smith of the checkout (`setup-smith`; with only the two tests when the diff needs them) and runs `smith check --changed` (#3163): for scope `docs` the command runs the docs check and, when the diff touches `README.md` or the skills, `readme_test` and `skills_test`; the pre-push hook runs the same command, so the two tests are no longer the only thing the release smith ran in CI alone (#3160). `ci` owes `docs-linux` for this scope. A docs run is recognised by its jobs: `docs-linux` ran and `macos`, `build-linux` and the shards show as skipped (in a queue run `build-linux` ran too). A push to main of only such files is scope `none`: no release, no warm build (nothing in the compiler changed). `tests/ci_scope_test.smith` runs the `Scope` script on a docs-only, a README, a code, a mixed, a template and a push diff, so the exception cannot silently widen.

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
once, packs `build/`, `.smith/` and the generated `tree-sitter/src` with `tar --zstd` (and, in a second file of the artifact, the shim's
`smith/shim-pch` folder of the cache: both are inputs of smith's own steps, and without them every shard generated the
parser and compiled the header, the 8 shims and the link of smith again, 16 steps and 26 s, #3458), and uploads it as the artifact
`build-linux` (1 day); each shard restores it (`setup-smith` with `artifact: build-linux`), gives the checkout's sources an old time so none is newer than
the build (the build's own files keep their times: touching them made every step run again, #2833), and runs its tests. Measurement before and after (3 queue runs, 3 pull request runs): in the PR of #1758. **One runner for every part** (#3458): a part used to compile and link a runner of its own for the files that hold its tests (`smith test --shard`, #1200: 18 to 39 s). `build-linux` builds the runner with `--skip-macos` (every test the Linux parts run) and `test-part` sets `SMITH_TESTS_WHOLE=1`, so the build ignores `--shard`, `--costs`, `--weigh` and `--test` (it keeps `--skip-macos` and `--macos-only`), compiles and links nothing, and the runner deals and selects at run time; the parts list the same tests as before.

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.

## Decisions

- Never merge a broken build (owner, 2026-10-25: "We should just have the discipline to not merge broken builds"): every change is a pull request from its own branch; GitHub's hosted runners run the full check on it (`ci.yml`), and GitHub's merge queue merges it, squashed, only when the required check `ci` passes on it together with main and the entries ahead of it; nobody pushes to main, and there are no exceptions for known failures (owner, 2026-10-29: "they make a pr when ready, those are running on github runners, not the self hosted one, once the ci passes, we merge into main").
- A queue entry that fails is built alone by the next run (one queue run at a time, [queue](ci/queue.md), #3131); before, with `HEADGREEN`, the groups behind it were built on its tree, so one hung job failed every group stacked on it (#3051: `frontend-coverage-shard (5)` sat 30 minutes in two runs, 45 minutes each, and #3048 queued behind both). Every step that can hang has its own timeout, far under its job's, and a retry when the failure is the timeout; the count shard's step gets 7 minutes and no retry (a hang from a diff is deterministic). A run that our own workflows cancel (`queue-guard`) writes why in its run summary. `queue-heal` (#3051) dequeues a front entry whose run is `cancelled` with its `ci` job failed (a timed-out job), unless `queue-guard` cancelled it; GitHub kept such an entry 58 minutes when only `failure` was read. A merge-queue run stops at its first failed job (#3051); a PR check does not, because a cancelled job keeps the run from posting its commit status `ci` (the dispatcher would start the check again and again; run 42948277938 showed it): the failing job's last step, `.github/actions/fail-fast`, cancels the run; `ci` (`if: always()`) still runs and reports `failure` within a minute, the other jobs end `cancelled`, and queue-heal dequeues the entry. Before, `ci` waited for every job (a lint failure at minute 5 let four stacked runs go on for 381 minutes).
