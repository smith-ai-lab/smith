---
type: contract
title: A proof is fast, and fast is CPU time
description: Each proof has a budget of about 10 s of CPU time charged as on 8 idle cores, so a busy machine slows a proof and never fails it.
tags: [prove, budget]
---
# A proof is fast, and fast is CPU time

**A proof is fast, or it is written wrong, and fast is CPU time.** Like a test, each proof has a budget of about 10 s,
and one that exceeds it fails. The fix is to write it differently: free inputs instead of random draws, local proofs,
and a contract per function, so that callers use the contract instead of the body. The budget is CPU time, so that a
busy machine slows a proof and never fails it (owner's decision, after load averages of 17 to 54 made proofs time
out): a proof is charged what it would take on 8 idle cores, a constant on every machine (`Options::solver_threads`):
the CPU time of what runs one thing after another, plus for the queries Bitwuzla runs side by side the longest of
them or all of them divided by 8, whichever is more. The child process has the kernel's CPU limit as a backstop and a
large wall-clock limit for one that is blocked. `smith prove -v` first prints each phase's time and the module's size (read and link, the gate, flattening, verify); the gate skips flattening when no function has a contract of its own (asserts alone are not one), so a program with nothing to prove costs seconds. It then lists every function with wall, CPU and charged time (a
cached one marked cached, with the time it took), the slowest query of each and, for one that took a second or
ran out, what made it costly: the IR's divisions by a variable, products of variables, loads and stores by line, copies
of large objects and unrolled loops. Proofs are cached by content (`$XDG_CACHE_HOME/smith/proofs`, a hash of the IR the
prover checks, 31 days unused removed), and only what changed is proved again; `--no-cache` proves again.
