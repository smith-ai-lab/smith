---
name: smith-dev-tickets-body
description: The sections of a smith ticket body (Why, the design as decided, use cases, interactions, files and areas, escalate before building, done when, needs), with a bad and a good example. Load when writing or rewriting a ticket body.
---

# smith-dev-tickets-body

The rules for tickets are in [smith-dev-tickets](../smith-dev-tickets/SKILL.md). A body has these sections, in order.

1. **Why**: one paragraph. The problem and what it costs.
2. **The design as decided**: every decision stated, with the owner's words (quoted) where they matter. Leave nothing to be guessed.
3. **Use cases**: when the design depends on them, collect them first (ask the owner) and list them here. No use cases yet means the ticket is not ready.
4. **Interactions**: for a ticket that adds or changes a language or library feature, list each existing feature it meets: effects and their ceilings, generics, lambdas and function pointers, linear and affine types, autodiff, GPU kernels, C, C++ and Objective-C interop, contracts and `smith prove`, `smith lsp`. For each one, give one test of the two together, or one line saying why they don't meet. Owner, 2026-11-03, agreeing after the new front end shipped without effect inference: "I think that's a great idea." For a ticket that touches no feature, write "none".
5. **Files and areas**: where the change goes and what it must not touch, so tickets do not overlap.
6. **Escalate before building**: what the builder must ask about rather than decide. If nothing, say "nothing".
7. **Done when**: the commands that show it, and what they print. For a cause from the count, the cause is gone from the first errors and the new count is in the PR. For a feature, also the docs it updates (the skill, the concept, the README if users reach for it), so future agents learn it without reading the code. Never write "No docs here" on a `feat` ticket: the docs gate fails any `feat` commit that changes nothing under `knowledge/` or `.agents/skills/`. Name the concept row to edit (for front-end work, `knowledge/front-end.md`), or say the commit type is `refactor`, `test` or `fix`. A ticket that edits a corpus case lists every test that reads one in its checklist ([smith-dev-language](../smith-dev-language/SKILL.md) names them).
8. **Needs** (a part's ticket only, when it has any): gaps that other parts' runners hit in this part's files, each with its error, a smallest failing example and the files it blocks.

Before filing, run the "Done when" commands as written, and check that every file it lists exists or is marked new.

## Bad
> Add caching as discussed. Make it fast. Follow the usual conventions.

Nothing is decided, "discussed" points at a conversation the reader never saw, and nothing says how to know it is done.

## Good
> **Why**: The report command re-reads every file on each run (4 s on the sample project).
> **Design**: Cache results in `.cache/report.json`, keyed by file hash. Owner: "never invalidate by time". No new flags.
> **Files**: `src/report.c`, `tests/report_test.c`. Not `src/main.c`.
> **Escalate before building**: where the cache lives if `.cache/` is not writable.
> **Done when**: `make check` passes; `time ./report sample/` prints under 1 s on the second run.
