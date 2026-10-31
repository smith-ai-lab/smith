---
name: smith-dev-factory
description: "How the factory runs: finish open issues before new work, the roadmap #73, fundamentals before features, chase the bottleneck, the test beds, and the rules for the owner's Mac (no pkill, no smith build install). Load when you are the lead or pick what to work on."
---

# smith-dev-factory

- **Finish open issues before starting new work;** a new idea becomes an issue, not a branch. The order is the pinned roadmap, #73: take the first unchecked item of a track nothing running blocks, and give a new issue its place there.
- **Fundamentals before features.** When the factory has friction (a gate failing for reasons outside a pull request's code, red trains, slow builds, checks or tests, a growing queue), lanes start no new feature work and spare capacity fixes the friction first; the lead lifts the freeze when it is fixed (owner, 2026-10-27: "we really need to stop adding new features and fix all of these fundamental things first").
- **Chase the bottleneck.** The one item the most work waits on (a blocker of tasks, CI, the merge queue) gets every spare worker, split or swarmed, and no lane starts new work that only queues behind it; the lead names it at every check (owner, 2026-10-28: "We need to stop adding new work and figure out how to unblock. How do we make sure we are always chasing the bottleneck"). See [smith-dev-lead](../smith-dev-lead/SKILL.md).
- **Friction.** Every pull request ends with `## Friction`; the lead reads them across PRs at each day's end with `smith build friction -Dsince=<date>`: a point in two or more becomes an issue or joins #49, so it is fixed once, in the skill, message or check that caused it.
- **Test beds** ([test-beds](../../../knowledge/test-beds.md)): `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game`. Read them and run the installed smith on them in place; build, test or prove them with a smith under test only in a scratch copy, so lanes never share a `.smith/`. They follow smith: when smith changes its idiom or breaks their code, change them to match (they are the owner's own projects for trying smith: commit and push to their main freely). The commit says what changed and why, naming the smith issue; their rules file carries no change log.
- **Known gaps:** [known-gaps](../../../knowledge/known-gaps.md).

## On the owner's Mac (the lead only)
- Never `pkill` or `killall`.
- Nobody runs `smith build install` or `smith build llvm` by hand: CI's `install` job puts each new stage0 release in the owner's `~/.local/bin` (owner, 2026-10-27; from the release since 2026-10-29).
