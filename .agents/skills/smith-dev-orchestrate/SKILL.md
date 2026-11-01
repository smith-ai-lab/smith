---
name: smith-dev-orchestrate
description: Load when you are a task runner: one fresh session that claims the top unclaimed `task` ticket, works it with subagent workers, merges through the queue, hands over and ends. Not for the lead.
---

# smith-dev-orchestrate

From 2026-11-02 the factory is one priority backlog worked by a pool of one-ticket task runners (#1409). You are one runner. You own exactly one ticket, and when it is done you end. The lead sorts the backlog and sets the pool's size; [smith-dev-factory](../smith-dev-factory/SKILL.md) has the rules.

## The loop
1. **Check the pool.** Read #73. If it says `pool: paused`, end at once and start nothing.
2. **Claim the top ticket.** Take the first open `task` issue, in #73's order, that is not `claimed` or `blocked`. Label it `claimed` and comment `claimed by <your session>`. The earlier claim wins (#686): if you lose a race, claim the next one.
3. **Work it.** The ticket is the whole brief. Start subagent workers (`.claude/agents/implementer.md`) for its parts, in parallel only where their files do not overlap. Haiku 5.5 for docs, a README or a change spelled out line by line; Sonnet 5.5 for code; both at medium reasoning effort. A Haiku worker that fails the same check twice is replaced by a Sonnet one.
4. **Too big?** Split it into new `task` tickets, each one PR of about an hour with its files listed, no two sharing a file, filed at their place in #73's order. Then claim the first. There are no nested orchestrators: a runner never starts a runner for a sub-ticket.
5. **Merge through the queue.** Open the PR from its own branch; the merge queue merges it once `ci` passes. Fix red CI on your PR yourself. Never push to main, never weaken a check, never force-push, never `pkill` or `killall`.
6. **Wake on your PR before ending.** Call `subscribe_pr_activity` on it, or `send_later` yourself a check. A PR with neither waits for the lead's hourly check.
7. **Hand over short.** Comment on the ticket in a few lines: what merged, what is open, what a successor needs (the PR, the queue, the open question). Remove the worktree and branch when the PR merges or closes.
8. **Start a successor** if fewer than N runners are running, the backlog has an unclaimed, unblocked `task`, and #73 says `pool: running`. Brief it with "load `smith-dev-orchestrate`". Then end.

## Rules while you work
- **Never guess.** A question the ticket does not settle goes to the lead with the options and your recommendation. Meanwhile, work what is not blocked.
- **Message the lead only when you need it** (owner, 2026-10-31: "they should only message you when they need you"): an unsettled question, a scope or ticket-split choice, a file conflict with another runner, a stuck ticket. Put several non-urgent points in one message.
- **Keep your context small.** Read workers' reports, not their work, unless a check fails. Ask workers for short reports.
- **Write tickets that run.** Before filing one, run its "Done when" commands as written, check that every file it lists exists or is marked new, and copy nothing from another ticket (15 of 89 PRs on 2026-10-31 hit wrong ticket text).
- **Triage friction.** Each PR ends with `## Friction`. For each bullet, find the open `friction` issue for the same cause and add the PR to its "Seen in" list (`#PR (~N min)`, the minutes from the bullet); if none exists, file one labelled `friction` ([smith-dev-ticket](../smith-dev-ticket/SKILL.md)). A tiny one-off that no one else can hit may stay in the PR alone.
- **Hand over the friction total.** Your handover ends with one bullet per point, each ending `(~N min)`, and the total.
