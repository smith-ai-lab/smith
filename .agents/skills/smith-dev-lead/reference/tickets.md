# Writing a ticket

Part of [smith-dev-lead](../SKILL.md); a runner splitting its ticket follows it too.

The reader has no context: not this conversation, not the owner's words. The ticket is the whole brief. Never write "as discussed".

## Labels
Every ticket has exactly one difficulty label and one priority label, set when it is filed.
- **Difficulty:**
  - `difficulty:easy`: docs, a README, or a change spelled out line by line in the ticket.
  - `difficulty:medium`: ordinary code with a clear spec. Most tickets.
  - `difficulty:hard`: a high bar, for design judgment or a change across many parts that cannot be split. Owner: "It should be a high bar to set something as hard."
- **Priority:** `p0` (main is red), `p1` (an urgent non-friction blocker, set by the lead), `p2` (the current goal), `p3` (everything else).
- `blocked` only when another ticket unlocks it, named by a line `**Waits for #N**` (each ticket it waits for) in the body or a comment. Owner, 2026-11-02: "we should never let you mark something as blocked unless you have a ticket saying what unlocks it." A question, a decision or a split is not a dependency: a question goes to the lead (`send_message`) and is answered; a decision the owner must make becomes its own issue, and the ticket waits for that; an issue too big for one runner stays a `task`, and the runner who claims it splits it ([smith-dev-runner](../../smith-dev-runner/SKILL.md), step 2); a parked ticket is `later`, not `blocked`. The runner whose merge closes the last ticket it names removes `blocked`. Without the line no one does: #1506–#1509 sat blocked for hours after #1518 merged.
- **Syntax:** a ticket's `.smith` examples for the new front end, its tests or docs are in the new syntax ([smith-dev-syntax](../../smith-dev-syntax/SKILL.md)), never today's; a form the tour has not settled is asked of the owner before the ticket is filed.
- `task` marks a ticket in the backlog; `friction` marks a friction ticket; its priority is its comment count, not a `p` label. `mac` marks a ticket that needs the macOS SDK.

## Split rule
A count ticket is a **compiler part**, and it owns that part's files. Only its runner edits them. There are five parts:
- **parser:** `front/lex`, `parse*`, `parser`, `ast*`, `format*`.
- **fixer:** `front/fix*`, `lint`.
- **checker:** `front/types*`, `effects*`, `affine*`, `when`, `roots`, `compile`.
- **emitter:** `front/emit*`.
- **C and libraries:** `front/cimport*`, `lib/`, `include/`, `runtime/`, the build files.

Each part also owns `tests/front_<its prefixes>_*` and the docs for its files (owner, 2026-11-06, after the same `?*T` change was built on three branches at once: "yeah please do this"). The ticket lists its files and, in **Why**, the count's causes it owns, as first-error files / files, ranked by files blocked.

Cutting by the files being converted (every `lib/std` module, every `src/front` module) sent every area into the same compiler files, so they were changed twice and conflicted. **A ticket must be finishable by one runner**, who does it end to end with no handover (owner, 2026-11-07: "It should just finish the ticket end to end"): a part's ticket names the causes it owns, from the latest count, and is done when they are gone from the first errors. Causes the count shows later get a new ticket of the same part. Make it big: one PR of thousands of lines (owner, 2026-11-06: "We need to push that up to thousands of lines per pr."). Don't slice one cause into pieces by count ("7 of 19 cases"): each slice pays a full queue build (owner, 2026-11-04: "We can definitely combine").

**A gap in another part is that part's work.** A runner that needs a change outside its files never makes it on its branch. It adds a comment to that part's ticket: `Needs: <error>`, a smallest failing example, and the files it blocks. Then it goes on with its own part's next cause. The part's runner works its `Needs:` comments together with its count causes, by files blocked. The count shows every parse error the parser recorded, but the errors after the first of a function body can be recovery cascades (#2477): re-measure after fixing it.

## Sections
1. **Why**: one paragraph. The problem and what it costs.
2. **The design as decided**: every decision stated, with the owner's words (quoted) where they matter. Leave nothing to be guessed.
3. **Use cases**: when the design depends on them, collect them first (ask the owner) and list them here. No use cases yet means the ticket is not ready.
4. **Interactions**: for a ticket that adds or changes a language or library feature, list each existing feature it meets (effects and their ceilings, generics, lambdas and function pointers, linear and affine types, autodiff, GPU kernels, C, C++ and Objective-C interop, contracts and `smith prove`, `smith lsp`). For each one, give one test of the two together, or one line saying why they don't meet. Owner, 2026-11-03, agreeing after the new front end shipped without effect inference: "I think that's a great idea." For a ticket that touches no feature, write "none".
5. **Files and areas**: where the change goes and what it must not touch, so tickets in a batch do not overlap.
6. **Escalate before building**: what the builder must ask about rather than decide. If nothing, say "nothing".
7. **Done when**: the commands that show it, and what they print; for a cause from the count, the cause gone from the first errors and the new count in the PR. For a feature, also the docs it updates (the skill, the concept, the README if users reach for it), so future agents learn it without reading the code. Never write "No docs here" on a `feat` ticket: the docs gate fails any `feat` commit that changes nothing under `knowledge/` or `.agents/skills/`. Name the concept row to edit (for front-end work, `knowledge/front-end.md`), or say the commit type is `refactor`, `test` or `fix`. A ticket that edits a corpus case lists every test that reads one in its checklist ([smith-dev-language](../../smith-dev-language/SKILL.md) names them).

## Bad
> Add caching as discussed. Make it fast. Follow the usual conventions.

Nothing is decided, "discussed" points at a conversation the reader never saw, and nothing says how to know it is done.

## Good
> **Why**: The report command re-reads every file on each run (4 s on the sample project).
> **Design**: Cache results in `.cache/report.json`, keyed by file hash. Owner: "never invalidate by time". No new flags.
> **Files**: `src/report.c`, `tests/report_test.c`. Not `src/main.c`.
> **Escalate before building**: where the cache lives if `.cache/` is not writable.
> **Done when**: `make check` passes; `time ./report sample/` prints under 1 s on the second run.
