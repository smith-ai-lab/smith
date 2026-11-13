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
