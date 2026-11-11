---
type: design
title: The front-end coverage count in CI
description: The front-end coverage count, counted in the nightly run and by hand, from nothing: what it measures and what it prints.
tags: [ci, coverage]
---
# The front-end coverage count in CI

**The front-end coverage count** (#2073). The nightly `schedule` run of `ci.yml`, and a `workflow_dispatch` with the input `count` set, run `smith build frontend-coverage` over every tracked `.smith` file: the job `frontend-coverage-fix` builds the release smith for that commit and fixes the tree, `frontend-coverage-shard` jobs compile it, and `frontend-coverage` prints the result. Every count starts from nothing: no cache, no fixed files restored, no previous count compared. No push, pull request or queue run counts (#3052), and nothing gates on it.

**What it prints.** The step log has one line `frontend-coverage: <sha> <passed> <total>`, which the lead's watcher reads, then the lines `frontend-errors`, `frontend-cause` (20 lines: `<first> <files> <count> <cause>`), `frontend-pair` and `frontend-name`. The lead reads them with `gh api repos/smith-ai-lab/smith/actions/jobs/<id>/logs` (`.agents/skills/smith-dev-lead/reference/watch.md`). The job summary has `N of M files pass`, the failures per stage and the top 20 causes, ranked by the files each stops first (#2247). The artifact `frontend-coverage-<sha>` (kept 30 days) has `output.txt` and `results.tsv`.

**What it measures.** A file reads modules that must already be in the new syntax, so the fixer runs on the whole tree before anything is compiled; the shards then compile the fixed files, 8 parts in all.

**History.** The count ran on each push to main with an alarm until #3052, and before that it gated every queue entry.
