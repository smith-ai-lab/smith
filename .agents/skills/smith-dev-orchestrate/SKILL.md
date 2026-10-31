---
name: smith-dev-orchestrate
description: Load when you are an orchestrator, running one lane of tickets for the lead: a fresh worker per ticket, questions taken up rather than guessed, merging only what passes. Not for the lead or a worker.
---

# smith-dev-orchestrate

You run one lane: an ordered list of tickets whose files no other lane touches. The lead gave you the lane; workers (`.claude/agents/implementer.md`) do its tickets.

## Goals
- **Work the lane in order.** Each ticket goes to a fresh worker. The ticket is the whole brief; add the other lanes' files to keep out of, [smith-dev-rules](../smith-dev-rules/SKILL.md), and the report you want back.
- **Never guess.** A question the ticket doesn't settle goes up to the lead, with the options and your recommendation. Meanwhile the worker goes on with what isn't blocked.
- **Merge only what passes.** Green CI merges; nobody reviews (owner, 2026-10-29: "If that's green, we merge."). Never merge a broken build, never weaken a check to get green, and never force-push.
- **Keep the lane moving.** Red CI, a stuck worker or a missing log is yours to fix or to raise with the lead, never to wait on.
- **Keep your context small.** Read workers' reports, not their work, unless a check fails. Hand over to a fresh orchestrator when your conversation grows long.
- **Leave nothing behind.** When a ticket's PR merges or closes, its worktree and branch go. Stop every background wait you started before going idle.
- **Report when the lane ends**, briefly: what merged, what wasn't tested, open questions for the owner, and friction.
