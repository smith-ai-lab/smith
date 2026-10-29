---
type: design
title: How a change reaches main
description: Every change is a pull request from its own branch; GitHub's hosted runners run the full check on it, and the merge queue merges it squashed once the required check ci passes on top of main.
tags: [ci, process]
---
# How a change reaches main

**One branch and one pull request per change.** An agent works on its own branch, pushes it and opens a pull request
(`gh pr create`, `Closes #N`). Locally it runs `smith fmt`, `smith lint`, `smith docs` and the tests for what it
changed, never the full check.

**CI is GitHub's.** `.github/workflows/ci.yml` runs the full check on the pull request on hosted runners
([ci-hosted](ci-hosted.md)); the one required check is `ci`. Nothing runs on the owner's Mac.

**The merge queue merges.** When `ci` is green the pull request goes into GitHub's merge queue
(`gh pr merge N --squash --auto`). The queue makes a group of up to 5 entries on top of main, runs `ci` on the group
(`merge_group`) and, when it passes, squashes each pull request into one commit on main; a red group is split and the
pull request at fault leaves the queue with its run linked. The ruleset on `main` requires a pull request, the check
`ci` and the queue, a linear history, and blocks force pushes: nobody pushes to main.

**After a merge** the push to main does not test again (the queue checked that commit); `ci.yml`'s `release` job
publishes the stage0 release `stage0-<commit>` ([release](release.md)). The installed smith on a machine is that
release (`sh scripts/release.sh fetch stage0-<commit> ~/.local`); nobody runs `smith build install`.

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.
