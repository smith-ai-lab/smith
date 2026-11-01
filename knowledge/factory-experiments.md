---
type: design
title: Factory experiments: ideas tried and what happened
description: The factory's process experiments, each with what was measured and what came of it: standing lanes with one orchestrator each (replaced) and one backlog with one-ticket task runners (from 2026-11-02, #1409, under review).
tags: [factory, process, experiments]
---
# Factory experiments: ideas tried and what happened

The factory's process changes on purpose. Each experiment is one idea, tried, measured, then kept or dropped. The owner's aim, from #1409 (2026-11-01): "the main thing we should be optimizing for is learning and velocity of learning, not necessarily being correct every time. It's about having ideas, trying them, and then at the end of the day... we pause, we reflect, we come up with a better idea, and maybe we can even in our knowledge graph say ideas we've tried."

## Standing lanes, one orchestrator per lane (2026-10-31 to 2026-11-01)

The roadmap was split into lanes: ordered lists of tickets whose files no other lane touched. Each lane had one long-lived orchestrator that gave every ticket to a fresh worker, and a lead handed out the lanes.

- **What it made:** about 100 PRs merged in a day.
- **What it cost:** most of the usage went to re-reading orchestrator contexts. Track A's orchestrator used 480M cache-read tokens against 1.3M output, re-reading 190k–270k tokens of context on every wake.
- **Result:** replaced by the next entry.

## One backlog, one-ticket task runners (from 2026-11-02, #1409)

Every open `task` ticket sits in one backlog, in #73's order. A task runner is a fresh session that owns one ticket: it claims the top unclaimed, unblocked ticket (the earlier claim wins, #686), works it with subagent workers, merges through the queue, leaves a short handover on the ticket and ends. A pool of N runners fills itself while #73 says `pool: running`, and `pool: paused` lets the running ones finish. The owner's idea, from #1409: "maybe we want to take inspiration from a work stealing queue where the orchestrators don't have their own lanes, but instead each orchestrator just has one task at a time. And then there's like just one big backlog, priority sorted."

Measured, and compared with the lanes of 2026-11-01:

- cost per merged PR (the session `cost_usd` totals divided by merged PRs);
- cache-read tokens per merged PR;
- PRs merged per hour;
- friction minutes (the `(~N min)` totals, #1378).

**Review:** due at the end of 2026-11-02. The day's numbers, and whether the experiment is kept, changed or dropped, are written in this entry that day. Until then it records only what is measured.

## Adding an entry

A new experiment gets a heading with its dates and issue, what it changed, what was measured, and the result. Keep the owner's words that started it.
