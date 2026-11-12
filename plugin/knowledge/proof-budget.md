---
type: concept
title: A proof is fast, and fast is CPU time
description: Each proof runs in its own process with about 10 s of CPU time charged as on 8 idle cores and 8 GB of memory, so a busy machine slows a proof and never fails it; the solver's answers are cached.
tags: [prove, budget]
---
# A proof is fast, and fast is CPU time

Each function is proved in a process of its own, in at most 10 s of CPU time (`--budget=SECONDS`) and 8 GB (`--memory=GB`). One that runs out fails like a test that times out; the fix is to write it differently: free inputs instead of random draws, local proofs, and a contract per function so that callers use the contract instead of the body.

## Example

```text
$ smith prove -v --no-cache
ok    primes::sum  (primes.smith:9)  0.02 s wall, 0.01 s CPU, charged 0.01 s
```

## Rules

- **The budget is CPU time**, so a busy machine slows a proof and never fails it: a proof is charged what it would take on 8 idle cores, a constant on every machine. That is the CPU time of what runs one after another, plus, for queries Bitwuzla runs side by side, the longest of them or all of them divided by 8, whichever is more. The process ends at its budget even if the prover is stuck, and when `smith prove` is killed.
- **Memory** is the process's physical footprint, watched by the parent. The failure names the limit: `FAIL  f` / `over its memory limit of 8.0 GB: the proof's process had reached 8.1 GB ... --memory=GB changes the limit`.
- **`smith prove -v` prints** a `phase` line for each step (read and link, the gate, flatten, verify) with its time and the module's size, then every function with wall, CPU and charged time, the slowest query of each, and the trusted ones. The gate skips flattening when no function has a contract of its own, so a program with nothing to prove costs seconds.
- **The solver's answers are cached** by the text of the query and Bitwuzla's version, in the step store (`$XDG_CACHE_HOME/smith/steps/a`, else `~/.cache/smith/steps/a`; entries unused for 31 days are removed): a Sat or an Unsat with the CPU seconds it took, charged on a hit as on a run, so the budget and the report are the same warm or cold. A timeout, a crash and a query Bitwuzla cannot read are not kept. `--no-cache` asks the solver again.
- **A group of functions is cached too**: when every function of a `smith prove` group was proved, the next run with the same files, flags, options and compiler prints the same lines with `(not compiled)` and compiles nothing. A changed file, header, flag or budget, `-v`, `--function` or `--no-cache` runs the group as before.

## Interactions

- Reading why a proof is slow: [prove-cost](prove-cost.md).
- Loops by induction cost one pass whatever the bound ([prove-loops](prove-loops.md)).

## Replaces from C

Tests that take as long as the inputs you try.
