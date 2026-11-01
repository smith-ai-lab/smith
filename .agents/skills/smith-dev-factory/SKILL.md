---
name: smith-dev-factory
description: "How the factory runs: one backlog in label order worked by a pool of one-ticket runners (#1409), finish open issues before new work, the roadmap #73, fundamentals before features, chase the bottleneck, the test beds, and the rules for the owner's Mac (no pkill, no smith build install). Load when you are the lead or pick what to work on."
---

# smith-dev-factory

- **One backlog, in label order (#1409, from 2026-11-02).** The backlog is the open `task` issues, excluding `claimed` and `blocked`. A runner takes the top one: `p0` (main is red), `p1` (friction over 60 minutes), `p2` (the current goal, zero patches #1377), `p3` (everything else); the oldest first within a label. Until #1377 is done, the backlog holds only its work: the new front end (#1112) to the switch-over, the `.smith` tools, upstream LLVM (#1376), the patch deletions, and what speeds those up.
- **Dependencies:** "Waits for #N" in the ticket plus the `blocked` label, until #N closes; then the lead removes `blocked`.
- **Difficulty:** one label per ticket, `difficulty:easy`, `difficulty:medium` or `difficulty:hard`, set by whoever files it ([smith-dev-ticket](../smith-dev-ticket/SKILL.md)). Which model runs which difficulty is in [providers/anthropic.md](providers/anthropic.md), the only file that names models.
- **The pool line on #73:** `pool: running · N=5` or `pool: paused`. #73 holds the current goal, that line and the lead's handovers; it keeps no lane list. Paused: running tickets finish, and no runner starts another.
- **Runners** are fresh sessions, one ticket each, that start their successor when they hand over ([smith-dev-runner](../smith-dev-runner/SKILL.md)). Runners are launched as [providers/anthropic.md](providers/anthropic.md) says. The lead runs the pool ([smith-dev-lead](../smith-dev-lead/SKILL.md)).
- **Stale claims:** a claim older than 3 hours with no PR activity is released by the lead.
- **Friction is event-based.** Every pull request ends with `## Friction`, each bullet ending `(~N min)`, the author's estimate. A friction ticket's minutes are summed (`smith build friction -Dsince=<date>`). A friction ticket past 60 total minutes becomes `p1`; the lead re-ranks and merges duplicates each day. The fix goes in the skill, message or check that caused it, and a friction point that its own runner can fix in its own files is fixed in that PR.
- **Finish open issues before new work;** a new idea becomes an issue, not a branch, placed by its label.
- **Fundamentals before features.** When the factory has friction (a gate failing for reasons outside a pull request's code, red trains, slow builds, checks or tests, a growing queue), runners start no new feature work and spare capacity fixes the friction first; the lead lifts the freeze when it is fixed (owner, 2026-10-27: "we really need to stop adding new features and fix all of these fundamental things first").
- **Chase the bottleneck.** The one item the most work waits on (a blocker of tickets, CI, the merge queue) gets every spare runner, split or swarmed; no runner starts work that only queues behind it. The lead names it at every check (owner, 2026-10-28: "How do we make sure we are always chasing the bottleneck").
- **Test beds** ([test-beds](../../../knowledge/test-beds.md)): `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game`. Read them and run the installed smith on them in place; build, test or prove them with a smith under test only in a scratch copy, so runners never share a `.smith/`. They follow smith: when smith changes its idiom or breaks their code, change them to match (they are the owner's own projects for trying smith: commit and push to their main freely). The commit says what changed and why, naming the smith issue; their own docs carry no change log.
- **Merge discipline:** every change is a pull request from its own branch, and nobody pushes to main. Push `main` after every merge: the plugin's skills come from GitHub, so other agents learn a feature only once it is pushed. The rule and its check are in [smith-dev-rules](../smith-dev-rules/SKILL.md).
- **Known gaps:** [known-gaps](../../../knowledge/known-gaps.md).

## On the owner's Mac (the lead only)
- Never `pkill` or `killall`.
- Nobody runs `smith build install` or `smith build llvm` by hand: CI's `install` job puts each new stage0 release in the owner's `~/.local/bin` (owner, 2026-10-27; from the release since 2026-10-29).
