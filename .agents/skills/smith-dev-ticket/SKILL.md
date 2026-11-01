---
name: smith-dev-ticket
description: Load when you are the lead and write a ticket (a GitHub issue) for someone with no context to do. Sections, rules and a bad and a good example.
---

# smith-dev-ticket

The reader has no context: not this conversation, not the owner's words. The ticket is the whole brief. Never write "as discussed".

## Labels
Every ticket has exactly one difficulty label and one priority label, set when it is filed.
- **Difficulty:**
  - `difficulty:easy`: docs, a README, or a change spelled out line by line in the ticket.
  - `difficulty:medium`: ordinary code with a clear spec. Most tickets.
  - `difficulty:hard`: a high bar, for design judgment or a change across many parts that cannot be split. Owner: "It should be a high bar to set something as hard."
- **Priority:** `p0` (main is red), `p1` (an urgent non-friction blocker, set by the lead), `p2` (the current goal), `p3` (everything else).
- `task` marks a ticket in the backlog; `friction` marks a friction ticket; its priority is its comment count, not a `p` label. `mac` marks a ticket that needs the macOS SDK.

## Split rule
A ticket is one PR of about an hour, with its files listed. No two tickets share a file. A bigger ticket is split before anyone claims it: each piece gets its own files, difficulty and the parent's priority.

## Sections
1. **Why**: one paragraph. The problem and what it costs.
2. **The design as decided**: every decision stated, with the owner's words (quoted) where they matter. Leave nothing to be guessed.
3. **Use cases**: when the design depends on them, collect them first (ask the owner) and list them here. No use cases yet means the ticket is not ready.
4. **Files and areas**: where the change goes and what it must not touch, so tickets in a batch do not overlap.
5. **Escalate before building**: what the builder must ask about rather than decide. If nothing, say "nothing".
6. **Done when**: the commands that show it, and what they print. For a feature, also the docs it updates (the skill, the concept, the README if users reach for it), so future agents learn it without reading the code. A ticket that edits a corpus case lists every test that reads one in its checklist ([smith-dev-language](../smith-dev-language/SKILL.md) names them).

## Bad
> Add caching as discussed. Make it fast. Follow the usual conventions.

Nothing is decided, "discussed" points at a conversation the reader never saw, and nothing says how to know it is done.

## Good
> **Why**: The report command re-reads every file on each run (4 s on the sample project).
> **Design**: Cache results in `.cache/report.json`, keyed by file hash. Owner: "never invalidate by time". No new flags.
> **Files**: `src/report.c`, `tests/report_test.c`. Not `src/main.c`.
> **Escalate before building**: where the cache lives if `.cache/` is not writable.
> **Done when**: `make check` passes; `time ./report sample/` prints under 1 s on the second run.
