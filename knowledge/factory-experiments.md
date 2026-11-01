---
type: design
title: Factory experiments: ideas tried and what happened
description: The factory's process experiments and what came of each: standing lanes (dropped), the pool of one-ticket runners over one backlog (#1409), friction as a runner job ranked by comments, and the Mac runner on API credits; with the alternatives rejected and the metrics to watch.
tags: [factory, process, experiments]
---
# Factory experiments: ideas tried and what happened

The factory's process changes on purpose. Each experiment is one idea, tried, measured, then kept or dropped. The owner's aim, from #1409 (2026-11-01): "the main thing we should be optimizing for is learning and velocity of learning, not necessarily being correct every time. It's about having ideas, trying them, and then at the end of the day... we pause, we reflect, we come up with a better idea, and maybe we can even in our knowledge graph say ideas we've tried."

## Standing lanes (2026-10-31 to 2026-11-01): dropped

The roadmap was split into lanes: ordered lists of tickets whose files no other lane touched. Each lane had one long-lived orchestrator that gave every ticket to a fresh worker, and a lead handed out the lanes.

- **What it made:** about 100 PRs merged in a day.
- **Why it was dropped:** the spend went to re-reading contexts of 200k to 270k tokens. Track A's orchestrator used 480M cache-read tokens against 1.3M output. Lanes also split the parallelism: a lane with nothing eligible sat idle while another had a queue. And orchestrators polled while idle, each poll waking the whole context.

## The pool (2026-11-02, #1409)

One backlog of open `task` issues, in label order (`p0` to `p3`, oldest first). Each ticket has a difficulty label, and the provider file maps difficulty to a model. One lead talks to the owner. A runner is a fresh session that owns one ticket and starts its successor when it hands over. The owner's idea: "maybe we want to take inspiration from a work stealing queue where the orchestrators don't have their own lanes, but instead each orchestrator just has one task at a time. And then there's like just one big backlog, priority sorted." Contexts stay small because a runner ends after one ticket, and nothing polls on a large context.

## Friction as a runner job (2026-11-01)

First, friction was reviewed daily by the lead: sum the minutes, re-rank, move past 60 minutes to `p1`. A day is too slow: the same cause costs every runner that day. Now each runner comments `Hit this too: #PR (~N min)` on the open `friction` issue with the same cause, or opens one. One runner of the pool works in friction mode: it takes the open `friction` issue with the most comments, oldest first on a tie, fixes the cause, and starts a successor in friction mode. The chain stops when no friction issue is open. Owner: "we're just constantly chasing friction down from highest priority to lowest priority based on the number of comments on those friction tickets." The lead no longer reviews; an hourly bash check wakes it only when the pool needs it, because waking the lead on its large context every hour costs cache reads, the main spend of the lanes day.

## The Mac runner on API credits (2026-11-01)

The cloud runners pause at the subscription's daily limit. A bash loop on the owner's Mac starts `claude -p` runners on the owner's Anthropic API credits and keeps the work going; the key comes from 1Password on the command line, and `mac` tickets (those needing the macOS SDK) go to it first. A loop, not a chain of successors, so a crash costs one ticket.

## Alternatives considered

- **GitHub Projects ordering (GraphQL):** a board could hold the order, but agents' tools cannot reach it (GraphQL returns HTTP 403 in cloud sessions). Labels and `gh api` REST work everywhere.
- **Linear or Jira:** another tool to log in to, to keep in step with the pull requests, and to teach every agent.
- **Slack for the mailbox:** GitHub comments are already where the work is, and polling them with `gh api` in a bash loop costs no tokens.

## Metrics to watch

- PRs merged per day.
- Dollars per merged PR.
- Opus share of the spend.
- Friction comments closed per day.

**Review date: 2026-11-08.** Keep, change or drop each experiment here that day.

## Adding an entry

A new experiment gets a heading with its dates and issue, what it changed, what was measured, and the result. Keep the owner's words that started it.
