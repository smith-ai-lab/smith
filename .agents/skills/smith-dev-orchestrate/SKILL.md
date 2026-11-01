---
name: smith-dev-orchestrate
description: Load when you are an orchestrator, running one lane of tickets for the lead: a fresh worker per ticket, questions taken up rather than guessed, merging only what passes. Not for the lead or a worker.
---

# smith-dev-orchestrate

You run one lane: an ordered list of tickets whose files no other lane touches. The lead gave you the lane; workers (`.claude/agents/implementer.md`) do its tickets.

## Goals
- **Work the lane in order.** Each ticket goes to a fresh worker. The ticket is the whole brief; add the other lanes' files to keep out of, [smith-dev-rules](../smith-dev-rules/SKILL.md), and the report you want back.
- **Pick the cheapest model the ticket needs** (owner, 2026-10-31: "If it's like a documentation change, why are we using Sonnet, right?"): Haiku 5.5 for docs, a README or a change spelled out line by line; Sonnet 5.5 for code; both at medium reasoning effort. A Haiku worker that fails the same check twice is replaced by a Sonnet one.
- **Never guess.** A question the ticket doesn't settle goes up to the lead, with the options and your recommendation. Meanwhile the worker goes on with what isn't blocked.
- **Merge only what passes.** Green CI merges; nobody reviews (owner, 2026-10-29: "If that's green, we merge."). Never merge a broken build, never weaken a check to get green, and never force-push.
- **Keep the lane moving.** Red CI, a stuck worker or a missing log is yours to fix or to raise with the lead, never to wait on.
- **Wake on your PR before ending a turn.** After opening or queueing a PR, call `subscribe_pr_activity` on it (a PR event then wakes the session) or `send_later` yourself a check; ending a turn with an open PR and neither stalls it until the lead's hourly check.
- **Keep your context small.** Read workers' reports, not their work, unless a check fails. When the lead asks you to hand over, start no new ticket; when your running workers have finished, comment on the lane's epic with what a successor needs (the queue, open PRs, open questions) and message the lead.
- **Leave nothing behind.** When a ticket's PR merges or closes, its worktree and branch go. Stop every background wait you started before going idle.
- **Message the lead only when you need the lead** (owner, 2026-10-31: "they should only message you when they need you"): a question the ticket and the recorded decisions don't settle (options and your recommendation), a scope or ticket-split choice, a file conflict with another lane that the two of you can't settle directly, a stuck lane, and the lane's end report. Merges, started workers and CI progress are on GitHub, where the lead reads them; put several non-urgent points in one message.
- **Triage friction when a worker reports it** (owner, 2026-10-31: friction is event-based, never remembered). For each point in the report, search the open issues labelled `friction` for the same cause. If one exists, add the PR to its "Seen in" list; if not, file one labelled `friction` (smith-dev-ticket), with the PR in "Seen in". Then message the friction lane with the ticket number. A tiny one-off that no one else can hit may stay in the PR alone.
- **Write tickets that run:** before filing one, run its "Done when" commands as written, check that every file it lists exists or is marked new, and copy nothing from another ticket (15 of 89 PRs on 2026-10-31 hit wrong ticket text).
- **Report when the lane ends**, briefly: what merged, what wasn't tested, open questions for the owner, and friction.
