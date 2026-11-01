---
name: smith-dev-factory
description: "How the factory runs: finish open issues before new work, the roadmap #73, fundamentals before features, chase the bottleneck, the test beds, and the rules for the owner's Mac (no pkill, no smith build install). Load when you are the lead or pick what to work on."
---

# smith-dev-factory

- **Finish open issues before starting new work;** a new idea becomes an issue, not a branch. The order is the pinned roadmap, #73: take the first unchecked item of a track nothing running blocks, and give a new issue its place there.
- **Fundamentals before features.** When the factory has friction (a gate failing for reasons outside a pull request's code, red trains, slow builds, checks or tests, a growing queue), lanes start no new feature work and spare capacity fixes the friction first; the lead lifts the freeze when it is fixed (owner, 2026-10-27: "we really need to stop adding new features and fix all of these fundamental things first").
- **Chase the bottleneck.** The one item the most work waits on (a blocker of tasks, CI, the merge queue) gets every spare worker, split or swarmed, and no lane starts new work that only queues behind it; the lead names it at every check (owner, 2026-10-28: "We need to stop adding new work and figure out how to unblock. How do we make sure we are always chasing the bottleneck"). See [smith-dev-lead](../smith-dev-lead/SKILL.md).
- **Work is shared by stealing (#686):** each lane works its own queue first, then claims the next open `task` issue that is not `claimed` or `blocked` (label it `claimed`, comment `claimed by <lane>`; the earlier claim wins); a task is one PR of about an hour with its files listed, no two tasks share a file, and a bigger one is split into tasks (owner, 2026-10-28: "Think about work stealing task schedulers like rayon").
- **Friction is event-based.** Every pull request ends with `## Friction`, and the worker's report repeats it. The orchestrator files or counts it on a `friction` ticket ("Seen in"), each bullet ending `(~N min)`, the author's estimate of the time it cost. The friction lane fixes the ticket with the most minutes first (the sum over its "Seen in" lines; `smith build friction -Dsince=<date>` totals them per issue), in the skill, message or check that caused it. That ticket pre-empts the friction lane's other work, except a fix for red main. See [smith-dev-orchestrate](../smith-dev-orchestrate/SKILL.md) and [smith-dev-lead](../smith-dev-lead/SKILL.md).
- **Test beds** ([test-beds](../../../knowledge/test-beds.md)): `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game`. Read them and run the installed smith on them in place; build, test or prove them with a smith under test only in a scratch copy, so lanes never share a `.smith/`. They follow smith: when smith changes its idiom or breaks their code, change them to match (they are the owner's own projects for trying smith: commit and push to their main freely). The commit says what changed and why, naming the smith issue; their own docs carry no change log.
- **Merge discipline:** every change is a pull request from its own branch, and nobody pushes to main. Push `main` after every merge: the plugin's skills come from GitHub, so other agents learn a feature only once it is pushed. The rule and its check are in [smith-dev-rules](../smith-dev-rules/SKILL.md).
- **Known gaps:** [known-gaps](../../../knowledge/known-gaps.md).

## On the owner's Mac (the lead only)
- Never `pkill` or `killall`.
- Nobody runs `smith build install` or `smith build llvm` by hand: CI's `install` job puts each new stage0 release in the owner's `~/.local/bin` (owner, 2026-10-27; from the release since 2026-10-29).
