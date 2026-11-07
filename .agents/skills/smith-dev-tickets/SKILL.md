---
name: smith-dev-tickets
description: Writing and keeping smith's GitHub tickets: one big ticket per coherent piece of work, the body as the only truth (edited in place, never commented on), labels, the parts and who owns which files, the sections a body has. Load before filing, rewriting, merging or closing a ticket, as the lead or as a runner.
---

# smith-dev-tickets

The reader has no context: not the conversation, not the owner's words. The ticket body is the whole brief. Never write "as discussed".

## The body is the only truth
Owner, 2026-11-07: "We just want to have clean tickets with no comments. And if we learn something new, then what we want to do is update the body in place so that we have the cleanest possible tickets."
- **No comments on tickets.** GitHub is not a message bus (owner: "We shouldn't be using GitHub as a message bus"). A runner reaches the lead with `send_message`, and the lead answers the same way. Progress, gaps and friction go in the PR body, never on the ticket.
- **A decision changes the body.** When the lead or the owner decides something, the lead edits the body in place: the design says the current answer, with no history and no superseded options. The decision also goes as one line in [decisions](../../../knowledge/decisions.md), in the owner's words.
- **A body that has drifted is rewritten.** When a ticket carries old comments or contradicts itself, file a new ticket with the final answer. Its last line is `Replaces #a, #b`. Then close the old ones, with no comment, as `not_planned`.
- **The claim is a line in the body.** A runner claims by adding `claimed by <session> at <time>` as the body's first line and the label `claimed` (see [smith-dev-runner](../smith-dev-runner/SKILL.md), step 1).

## Size
Owner, 2026-11-07, on tickets that each made a 100-line PR: "the time it takes to implement the feature is larger than the amount of time a pipeline takes to run in CI." A ticket is one coherent piece of work, done end to end by one runner in one PR of up to thousands of lines (owner, 2026-11-06: "We need to push that up to thousands of lines per pr"; "70 line merges is way to small").
- Never split a ticket to make it small. Don't slice one cause into pieces by count ("7 of 19 cases"): each slice pays a full queue build (owner, 2026-11-04: "We can definitely combine").
- Small tickets that touch the same files are one ticket. Merge them before anyone claims one.
- Split only when one PR would hold two unrelated pieces of work that collide.

## Which tickets exist
A ticket exists only when it is on a lane list of #73 or in the roadmap step that comes next. An idea for later is a line on [later](../../../knowledge/later.md), not an issue. A cause the count finds goes into the body of its part's ticket, never into a new ticket.

## Labels
Every ticket has exactly one difficulty label and one priority label, set when it is filed.
- **Difficulty:**
  - `difficulty:easy`: docs, a README, or a change spelled out line by line in the ticket.
  - `difficulty:medium`: ordinary code with a clear spec. Most tickets.
  - `difficulty:hard`: a high bar, for design judgment or a change across many parts that cannot be split. Owner: "It should be a high bar to set something as hard."
- **Priority:** `p0` (main is red), `p1` (an urgent blocker, set by the lead), `p2` (the current goal), `p3` (everything else).
- `blocked` only when another ticket unlocks it, named by a line `**Waits for #N**` in the body, one line per ticket. Owner, 2026-11-02: "we should never let you mark something as blocked unless you have a ticket saying what unlocks it." A question or a decision is not a dependency: a question goes to the lead (`send_message`), and the lead edits the body with the answer. The runner whose merge closes the last ticket a `Waits for` line names removes `blocked`. Without the line, no one does: #1506–#1509 sat blocked for hours after #1518 merged.
- **Syntax:** a ticket's `.smith` examples, tests and docs for the new front end are in the new syntax ([smith-dev-syntax](../smith-dev-syntax/SKILL.md)), never today's. A form the tour has not settled is asked of the owner before the ticket is filed.
- `task` marks a ticket in the backlog. `mac` marks a ticket that needs the macOS SDK. There are no friction issues: friction goes in the PR body (below).

## Parts own their files
A count ticket is a **compiler part**, and it owns that part's files. Only its runner edits them. There are five parts:
- **parser:** `front/lex`, `parse*`, `parser`, `ast*`, `format*`.
- **fixer:** `front/fix*`, `lint`.
- **checker:** `front/types*`, `effects*`, `affine*`, `when`, `roots`, `compile`.
- **emitter:** `front/emit*`.
- **C and libraries:** `front/cimport*`, `lib/`, `include/`, `runtime/`, the build files.

Each part also owns `tests/front_<its prefixes>_*` and the docs for its files (owner, 2026-11-06, after the same `?*T` change was built on three branches at once: "yeah please do this"). The ticket lists its files and, in **Why**, the count's causes it owns, as first-error files / files, ranked by files blocked. Cutting tickets by the files being converted (every `lib/std` module, every `src/front` module) sent every area into the same compiler files, so they were changed twice and conflicted.

A part's ticket is done when its causes are gone from the first errors. Causes the count shows later go into the body of that part's ticket, or into a new ticket of the same part once the old one has merged.

**A gap in another part is that part's work.** A runner that needs a change outside its files never makes it on its branch. It sends the lead the error, a smallest failing example and the files it blocks. The lead adds that to the part's body under `## Needs`, and the runner goes on with its own part's next cause. The part's runner works its `## Needs` with its count causes, ranked by files blocked. The count shows every parse error the parser recorded, but the errors after the first in a function body can be recovery cascades (#2477): re-measure after fixing the first.

## Friction
A PR body ends with `## Friction`: each point that cost about 5 minutes or more, with `(~N min)`, or `none`. A runner files no issue for it and comments on none. Every 4 hours a scheduled task wakes the lead to read the merged PRs. A cause seen in 2 or more PRs, or one that cost 30 minutes or more, becomes a ticket at the top of the speed lane at once (owner, 2026-11-07: "If they're real friction and they're important, then we need to do them immediately").

## Sections
A body's sections, in order, with a bad and a good example: [smith-dev-tickets-body](../smith-dev-tickets-body/SKILL.md).
