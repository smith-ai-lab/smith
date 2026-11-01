---
name: smith-dev-runner
description: Load when you are a runner: one fresh session that claims the top eligible `task` issue (or, in friction mode, the most-commented `friction` issue), does that one ticket end to end through its pull request, hands over and starts its successor. Not for the lead.
---

# smith-dev-runner

You are one runner in the pool (#1409). You own exactly one ticket. When it is merged and handed over, you end. The rules are in [smith-dev-factory](../smith-dev-factory/SKILL.md); the ticket format is in [smith-dev-ticket](../smith-dev-ticket/SKILL.md).

## Modes
- **Backlog mode** (the default): the loop below.
- **Friction mode**: your prompt says "friction mode". Follow the loop below, but step 1 takes the open `friction` issue that is not `claimed` with the most comments, ties oldest first: `gh api 'repos/smith-ai-lab/smith/issues?labels=friction&state=open&sort=comments&direction=desc'`. Fix the cause where it lives: in the skill, message, check or code that caused it. Your PR closes the issue. Your successor also runs in friction mode. If no friction issue is open, the chain stops: do not idle and do not start a successor.
- **`p0` first, in both modes.** If main is red, the open `p0` issue comes before everything else.
- **The Mac runner** takes `mac` tickets first, then the normal backlog ([smith-dev-mac-runner](../smith-dev-mac-runner/SKILL.md)). Cloud runners skip every `mac` ticket: it needs the macOS SDK.

## The loop
1. **Claim the top eligible ticket.** The backlog is the open `task` issues that are not `claimed` or `blocked`, and not `mac` unless you are the Mac runner. Take the highest priority label first (`p0` to `p3`), then the oldest. Label it `claimed` and comment `claimed by <your session> at <time>`. The earlier claim wins: if you lost the race, claim the next one.
2. **Check the size.** If the ticket is too big for one PR of about an hour, split it: file new `task` tickets, each with its files listed, no two sharing a file, and each with one difficulty label and the same priority label. Make the parent a checklist of them, then take the first piece.
3. **Build, change, check, open the PR.** Build the smith under test as [smith-dev-build](../smith-dev-build/SKILL.md) says, make the change, and run the checks the ticket's "Done when" names. Open the PR from its own branch. The PR ends with `## Friction`: one bullet per point, each ending `(~N min)`, or `none`.
4. **Too hard?** If the ticket is harder than its difficulty label, raise the label to the next one, note why on the ticket, and end. A stronger runner takes it.
5. **Merge through the queue.** Before opening the PR, run `git diff --name-only origin/main...HEAD` and check that every file belongs to your ticket; restore any other with `git checkout origin/main -- <file>`, and add files by name, never `git add -A`. A stale file outside the ticket reverts main when the PR is squashed (#1455). Right after opening the PR, turn on auto-merge (`enable_pr_auto_merge`, or `gh pr merge --auto <n>`), and check that `auto_merge` is set on `GET repos/smith-ai-lab/smith/pulls/<n>`. Without it, the PR never enters the queue. The queue merges it once the required check `ci` passes. If CI is red, fix it on your branch. Never push to main, never weaken a check, never force-push, never `pkill` or `killall`.
6. **Hand over.** Comment on the ticket in a few lines: what merged, what is open, what a successor needs, and the friction total as bullets ending `(~N min)` plus the total.
7. **Start a successor** if #73 says `pool: running`, fewer than N tickets are `claimed`, and there is an eligible ticket (in friction mode: an open unclaimed `friction` issue, and the successor is also in friction mode). Start it with the model that [providers/anthropic.md](../smith-dev-factory/providers/anthropic.md) maps from that ticket's difficulty, and the prompt "load `smith-dev-runner`" (add "friction mode" in friction mode). Then end. If `SMITH_RUNNER_LOOP=1` is set, skip this step: a bash loop starts the next runner ([smith-dev-mac-runner](../smith-dev-mac-runner/SKILL.md)), so a crash costs one ticket, not the chain.

## Rules while you work
- **Never guess.** A question the ticket does not settle goes to the lead with the options and your recommendation. Meanwhile, do what is not blocked.
- **Message the lead only when you need it:** an unsettled question, a split that changes the design, a file conflict with another runner, a stuck ticket. Put several non-urgent points in one message.
- **Keep your context small.** Read results, not whole logs, unless a check fails.
- **Write tickets that run.** Before filing a split ticket, run its "Done when" commands as written, and check that every file it lists exists or is marked new.
- **Friction.** For each `## Friction` bullet, find an open `friction` issue with the same cause. If one exists, add a comment: `Hit this too: #PR (~N min) — <one line>`. Each comment raises the issue in the friction ranking. If none exists, open a new issue labelled `friction` ([smith-dev-ticket](../smith-dev-ticket/SKILL.md)). A point you can fix in your own files, fix in the same PR.
