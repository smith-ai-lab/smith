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

**The pre-push hook runs those checks on every push.** `smith build` (any build of this tree) writes
`.git/hooks/pre-push`, in the common git dir so every worktree of the clone has it, with two lines: `#!/bin/sh` and
`exec smith build pre-push`; a fresh clone has it after its first `smith build`. The task builds the tree and, against
the merge-base with `origin/main`, runs `fmt --dry-run --Werror` on the changed files, `docs`, `lint --changed` and
`test --changed` with the smith it built (a push that changes only documents no build reads, the `none` scope of
[ci-hosted](ci-hosted.md), skips the tests). Every step runs; a failure stops the push and names the step and its log,
`.smith/pre-push-<step>.log`. It posts no status and uses no `gh`; CI is still the gate. git opens its connection
before the hook runs, so a hook of several minutes (a change to `build.c` selects every test) can lose an idle ssh
connection (`Connection ... closed by remote host`, status 141): push again with
`GIT_SSH_COMMAND="ssh -o ServerAliveInterval=20"`.

**CI is GitHub's.** `.github/workflows/ci.yml` runs the full check on the pull request on hosted runners
([ci-hosted](ci-hosted.md)); the one required check is `ci`. Nothing runs on the owner's Mac.

**The merge queue merges.** When `ci` is green the pull request goes into GitHub's merge queue
(`gh pr merge N --auto`). The queue makes a group of up to 5 entries on top of main, runs `ci` on the group
(`merge_group`) and, when it passes, squashes each pull request into one commit on main; a red group is split and the
pull request at fault leaves the queue with its run linked. The ruleset on `main` requires a pull request, the check
`ci` and the queue, a linear history, and blocks force pushes: nobody pushes to main.

**After a merge** the push to main does not test again (the queue checked that commit); `ci.yml`'s `release` job
publishes the stage0 release `stage0-<commit>` ([release](release.md)). Then `install`, on the owner's Mac (its one
self-hosted runner, used for nothing else), downloads that release, checks it and puts it in `~/.local/bin`, so the
installed smith is always main's; nobody runs `smith build install`.

**A red pull request** is its author's: read the failing job (`gh run view --log-failed`), fix it on the branch, push.
A conflict with main is fixed by merging main into the branch, never by a rebase or a force-push.
