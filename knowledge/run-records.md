---
type: design
title: The run records
description: With SMITH_RECORD set, smith appends one JSON line for each run (verb, arguments, exit, time, diagnostic ids, failed names), and smith build friction ranks the diagnostics by attempts to fix.
tags: [friction, process]
---
# The run records

**Why.** The agents' only signal about where they struggle was each pull request's `## Friction` section, which holds
what the author remembered. The records are what happened, so the lead can learn where agents fail and fix the message,
the skill, the check or the language (issue #535). Local only: nothing leaves the machine.

**What is written.** When `SMITH_RECORD` names an absolute file, a smith started from outside (the verbs `cc`, `c++`,
`build`, `check`, `test`, `prove`, `fmt`, `lint`, `docs`, `run`, `new`, and the links `smith-cc` and `smith-c++`) runs
its work as a child and appends one line when it ends; unset, nothing is written and nothing is forked. The child has
`SMITH_RECORDING` set, so a smith it starts (the build's compiler) is part of the record and writes none of its own. Its
stderr comes through a pipe, forwarded as it comes and read for diagnostics; with a terminal on stderr there is no pipe
and no diagnostics. The long-lived verbs (`lsp`, `dap`, `mcp`, `dbg`) are not recorded. The tests' scratch commands
unset both variables.

The line, one JSON object (`runtime/run_record.c`): `time` (UTC), `verb`, `project` (the folder's name), `lane`
(`SMITH_LANE`, else empty), `args` (at most 40, paths relative to the project), `exit`, `wall_ms`, `cpu_ms` (with the
processes it waited for), `diagnostics` (at most 24: `id`, `at` as `file:line:col`, `text`, the first line), and
`failed` (at most 24: the names after `FAIL` in the test runner's output and the parts of `smith check: <part>
FAILED`). A diagnostic's `id` is the tag in brackets at the end of its text (`-Wunused-variable`, `smith-effects`), else
the text with its quoted names and numbers replaced by `_`. smith writes the line in C with libc, not with its json
library, because the runtime is C without the libraries.

**The report.** `smith build friction -Dsince=YYYY-MM-DD` first prints the run records of `*.jsonl` in
`$SMITH_RECORDS_DIR` (else `~/.local/share/smith-records/`): the diagnostics ranked by attempts to fix, then the pull
requests' friction sections as before. Attempts to fix: for a diagnostic id in one project and verb, the runs from the
first that showed it to the first later run of that verb in that project without it (one attempt each, an unresolved
episode counts as it stands). An error message that takes four attempts on average is at the top; its rewrite shows the
count fall.

**The weekly loop.** Each week the lead turns the top five points into issues and records the numbers before and after
on each, so a fix shows up in the next week's numbers. The lanes' worktrees set `SMITH_RECORD` to one shared file under
`~/.local/share/smith-records/`.

**The train's log.** The report then reads the train's log (`~/Library/Logs/smith-train.log`, else
`$SMITH_TRAIN_LOG`): the pull requests rejected for conflicts and for a failed check, the tests those rejections name
(most often first), the load-sensitive reruns and how many went green, and the merge batches. The log lines carry a
time of day and no date, so the whole file is read, not only since the date. The idiom findings (`smith-*` ids) are their
own section of the report, ranked by episodes (first drafts that hit them), apart from the other diagnostics.

**Not built yet.** The analysis does not read the agents' transcripts; it is C in `build.c`, not a `.smith` program.
