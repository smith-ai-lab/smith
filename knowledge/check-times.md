---
type: design
title: Where the time of smith check goes, and how to measure it again (#3081)
description: How to profile fmt, lint, prove and docs, locally and from CI logs, what a one-line edit re-runs and why, the numbers of 2026-11-11, and the causes found: serial fmt, the lint key, the self-rebuild of smith's tree.
tags: [cache, ci, lint, prove]
---
# Where the time of smith check goes (#3081)

Measured 2026-11-11 on 4 cores (main 7d1941a); the ticket holds every number and the design. The parts: `fmt --dry-run`
(about 11 s in a CI queue run, 10.7 s locally before #3081, under 1 s warm after), `docs` (0.04 s), `lint` (98 to 147 s
in CI, 127 s from an empty cache, 2.7 s warm), `prove` (56 to 127 s in CI, every proof reused from the cache).

## Measuring again

Keep the release smith first on `PATH` (smith-dev-build), copy the binary under test before timing
(smith-dev-rules, Timing), and run each case twice.
- **A part's own line:** `SMITH_OUTPUT=full` prints the detail line a quiet verb hides: `smith lint: linted 605 files (101
  run, 504 cached, 9.8 s, setup 37.0 s: build info 10.6, needs 26.5, plan and hash 0.6)`, and prove's `build ... s,
  proofs ... s`. `smith check` ends each part's line with its wall and CPU seconds.
- **Why lint ran a file:** `SMITH_LINT_CACHE=why` prints, per file, the reason (`no manifest of an earlier run`: the first
  key changed; `no result for the content of the files it read`: a file it read changed) and the key's inputs as short
  digests (`smith+env`, `flags` or `database`, `.clang-tidy`, `headers`: `imports` for a .smith file keyed on its
  imports' headers, else the digest of all of them).
- **What fmt ran:** `SMITH_FMT_CACHE=why smith fmt --dry-run` prints `N files: R run, C cached`.
- **One file's clang-tidy, per check:** `smith lint --quiet <file> -- <its flags> --enable-check-profile` (flags from
  `.smith/compile_commands.json`); against `--checks=-*,readability-braces-around-statements` it splits parsing from
  the checks. The checks were 45 to 60% of a big file's time.
- **After an edit:** edit, `smith build`, `SMITH_LINT_CACHE=why SMITH_OUTPUT=full smith lint`, revert, and lint again.
  The cases of #3081: an exported function added to `lib/std/ascii.smith`, a comment in `smith.c`, a leaf `.c`.
- **CI:** the queue run's job logs (`get_job_logs` with `tail_lines`) give the `check-lint` and `check-fmt-prove` lines
  above; a failed PR run's log names the check that failed (what the checks catch).

## The causes found

- `smith fmt` ran its batches one after another with no cache: now cached and parallel (caches.md, The fmt cache).
- The lint key of a .smith file held every generated header, and every key held the build's module cache folder: now the
  headers of its imports and no folder (caches.md, The lint cache; #3062).
- `smith lint` or `smith prove` run as `build/<OS>-<arch>/smith` puts its own folder first on `PATH`
  (`smith_prefer_own_links`), so it rebuilds smith's tree with itself, and the next `smith build` with the release smith
  rebuilds it back. Each switch relinks smith (21 to 27 s of "needs") and changes the objects prove's group record is
  keyed on. Open: a decision for the lead in #3081.

`smith check` ends with `smith check: not run here: <parts>` and a count line (#3163): the parts the host cannot run, so a short time never hides a part that did not run; `--require` turns one into a failure.
