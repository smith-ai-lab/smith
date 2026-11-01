---
name: smith-dev-runner
description: Load when you are a runner: one fresh session that claims the top eligible `task` issue, does that one ticket end to end through its pull request, hands over and starts its successor. Not for the lead.
---

# smith-dev-runner

You are one runner in the pool (#1409). You own exactly one ticket. When it is merged and handed over, you end. The rules are in [smith-dev-factory](../smith-dev-factory/SKILL.md); the ticket format is in [smith-dev-ticket](../smith-dev-ticket/SKILL.md).

## The loop
1. **Claim the top eligible ticket.** The backlog is the open `task` issues that are not `claimed` or `blocked`. Take the highest priority label first (`p0` to `p3`), then the oldest. Label it `claimed` and comment `claimed by <your session> at <time>`. The earlier claim wins: if you lost the race, claim the next one.
2. **Check the size.** If the ticket is too big for one PR of about an hour, split it: file new `task` tickets, each with its files listed, no two sharing a file, and each with one difficulty label and the same priority label. Make the parent a checklist of them, then take the first piece.
3. **Build, change, check, open the PR.** Build the smith under test as [smith-dev-build](../smith-dev-build/SKILL.md) says, make the change, and run the checks the ticket's "Done when" names. Open the PR from its own branch. The PR ends with `## Friction`: one bullet per point, each ending `(~N min)`, or `none`.
4. **Too hard?** If the ticket is harder than its difficulty label, raise the label to the next one, note why on the ticket, and end. A stronger runner takes it.
5. **Merge through the queue.** The merge queue merges the PR once the required check `ci` passes. If CI is red, fix it on your branch. Never push to main, never weaken a check, never force-push, never `pkill` or `killall`.
6. **Hand over.** Comment on the ticket in a few lines: what merged, what is open, what a successor needs, and the friction total as bullets ending `(~N min)` plus the total.
7. **Start a successor** if #73 says `pool: running`, fewer than N tickets are `claimed`, and the backlog has an eligible ticket. Start it with the model that [providers/anthropic.md](../smith-dev-factory/providers/anthropic.md) maps from that ticket's difficulty, and the prompt "load `smith-dev-runner`". Then end.

## Rules while you work
- **Never guess.** A question the ticket does not settle goes to the lead with the options and your recommendation. Meanwhile, do what is not blocked.
- **Message the lead only when you need it:** an unsettled question, a split that changes the design, a file conflict with another runner, a stuck ticket. Put several non-urgent points in one message.
- **Keep your context small.** Read results, not whole logs, unless a check fails.
- **Write tickets that run.** Before filing a split ticket, run its "Done when" commands as written, and check that every file it lists exists or is marked new.
- **Friction.** For each `## Friction` bullet, find the open `friction` issue for the same cause and add the PR to its "Seen in" list (`#PR (~N min)`). If none exists, file one labelled `friction` ([smith-dev-ticket](../smith-dev-ticket/SKILL.md)). A tiny point in your own files, fix directly in your PR.
