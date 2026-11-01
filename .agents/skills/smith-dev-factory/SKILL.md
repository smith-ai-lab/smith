---
name: smith-dev-factory
description: "How the factory runs: one priority backlog worked by a pool of one-ticket task runners (#1409), finish open issues before new work, the roadmap #73, fundamentals before features, chase the bottleneck, the test beds, and the rules for the owner's Mac (no pkill, no smith build install). Load when you are the lead or pick what to work on."
---

# smith-dev-factory

- **One backlog, a pool of runners (#1409, from 2026-11-02).** The backlog is the open `task` issues in the order of the pinned roadmap, #73. Each runner is a fresh session that owns one ticket: [smith-dev-orchestrate](../smith-dev-orchestrate/SKILL.md) has its loop, [smith-dev-lead](../smith-dev-lead/SKILL.md) its role. Until #1377 (zero patches) is done, the backlog holds only #1377's work: the new front end (#1112) to the switch-over, the `.smith` tools, upstream LLVM (#1376), the patch deletions, and what speeds those up.
- **Pool control.** `pool: running` or `pool: paused` on #73. Paused: running tickets finish, and no runner starts another.
- **Finish open issues before new work;** a new idea becomes an issue, not a branch, placed in #73's order.
- **Fundamentals before features.** When the factory has friction (a gate failing for reasons outside a pull request's code, red trains, slow builds, checks or tests, a growing queue), runners start no new feature work and spare capacity fixes the friction first; the lead lifts the freeze when it is fixed (owner, 2026-10-27: "we really need to stop adding new features and fix all of these fundamental things first").
- **Chase the bottleneck.** The one item the most work waits on (a blocker of tasks, CI, the merge queue) gets every spare runner, split or swarmed; no runner starts work that only queues behind it. The lead names it at every check (owner, 2026-10-28: "How do we make sure we are always chasing the bottleneck").
- **Friction is event-based.** Every pull request ends with `## Friction`, each bullet ending `(~N min)`, the author's estimate. Friction tickets rank by their summed minutes (`smith build friction -Dsince=<date>` totals them per issue), and the fix goes in the skill, message or check that caused it. That ticket goes before other backlog work, except a fix for red main.
- **Test beds** ([test-beds](../../../knowledge/test-beds.md)): `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game`. Read them and run the installed smith on them in place; build, test or prove them with a smith under test only in a scratch copy, so runners never share a `.smith/`. They follow smith: when smith changes its idiom or breaks their code, change them to match (they are the owner's own projects for trying smith: commit and push to their main freely). The commit says what changed and why, naming the smith issue; their own docs carry no change log.
- **Merge discipline:** every change is a pull request from its own branch, and nobody pushes to main. Push `main` after every merge: the plugin's skills come from GitHub, so other agents learn a feature only once it is pushed. The rule and its check are in [smith-dev-rules](../smith-dev-rules/SKILL.md).
- **Known gaps:** [known-gaps](../../../knowledge/known-gaps.md).

## On the owner's Mac (the lead only)
- Never `pkill` or `killall`.
- Nobody runs `smith build install` or `smith build llvm` by hand: CI's `install` job puts each new stage0 release in the owner's `~/.local/bin` (owner, 2026-10-27; from the release since 2026-10-29).
