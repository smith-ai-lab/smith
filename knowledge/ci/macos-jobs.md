---
type: design
title: Hosted macOS jobs per push
description: A push to main starts 3 hosted macOS jobs (release, macos with the lint as a stage, warm), because GitHub's hosted macOS pool, not our own jobs, limits how many start.
tags: [ci, process]
---
# Hosted macOS jobs per push

A push to main starts 3 hosted macOS jobs (#3568): `release`, `macos` (the `--macos-only` tests, then `smith lint` of the code only the Mac sees as a stage) and `warm (macos-26)`. It was 4 while `macos-lint` was its own job (#3045).

Why fewer: GitHub, not our own jobs, limits hosted macOS. On 2026-11-13, of 159 macOS jobs on 40 push runs, 18 waited 2 to 10 minutes for a runner (mostly in rounds of 5 minutes) while 0 to 5 of ours ran, and 3 waited 15 minutes and were dropped without a step (ended `cancelled`, 0 steps; limits 90 and 30 minutes), which ends the push run `failure` though no step failed. A push every ~6 minutes (two queue entries built at once) asks that pool for twice as many jobs, so each job fewer is one less to drop.

A commit whose `warm (macos-26)` was replaced by a newer push (`warm`'s group, #3069) is covered by that push's `macos`, which runs the tests the store does not hold as passed.

## When GitHub drops a Mac job (#3589)

Fewer jobs do not remove the drop: run 43493532610 (2ced93c, 2026-11-13 19:34Z) lost its `macos` the same way, the fourth red push run of that day. `queue-heal.yml` (it already heals on every push and ci completion; a second watcher is not added) looks at the push runs of ci on main of the last 3 hours that did not end `success` and judges each with `scripts/mac_dropped.c`, which never touches the network and is replayed by `tests/mac_dropped_test.smith`. A job is a **drop** when it ended `cancelled`, has no `runner_name` and 0 steps, and ended 900 to 960 s after it was created (GitHub's 15 minutes). A `warm` of a run that a newer push run has replaced is never a drop: that is `warm`'s own group cancelling (#3069). The run's failed jobs are re-run (`rerun-failed-jobs`, the token has `actions: write`) only when every job of it that did not succeed is a drop or waits for one (`warm`, `publish`, `ci`); a job that ran and failed, or was cancelled after it got a runner, starts nothing, so nothing that ran is ever run again by this. It is not a retry of a failure: the job never ran. The restart is made at most 3 times (the run's attempt 4 is the last); each restart and the bound are one line in the summary of queue-heal's run (run, job, wait), and at the bound the run stays red, which is then true. `publish` and `warm` run after the restarted job as in a normal run; nothing is marked green by hand and nothing is skipped.
