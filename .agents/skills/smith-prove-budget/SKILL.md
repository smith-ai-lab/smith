---
name: smith-prove-budget
description: "The budget of a proof and what is kept: 30 s of CPU time and 8 GB per function, charged time, the memory failure, the -v phase lines, and the proof cache under $XDG_CACHE_HOME/smith/proofs (--no-cache). Load when a proof runs out of time or memory, or on `over its memory limit` or `cached: proved before`."
---

# The budget and the cache

```text
$ smith prove -v --no-cache
ok    primes::sum  (primes.smith:9)  0.02 s wall, 0.01 s CPU, charged 0.01 s
2 proved, 0 failed in 3.46 s
$ smith prove -v
ok    primes::sum  (primes.smith:9)  cached: proved before in 0.02 s wall, 0.01 s CPU, charged 0.01 s
```

- A function is proved when it carries a contract and is not trusted. Each is proved in a process of its own, in at most 30 s (`--budget=`; 10 s again once hosted CI is in place) of CPU time and 8 GB (`--memory=GB`) of memory (the process's physical footprint, watched by the parent, since macOS ignores `RLIMIT_AS`; a proof that fits its budget uses well under 1 GB); one that runs out of either fails like a test that times out, and the memory message names the limit and the function: `FAIL  f` / `over its memory limit of 8.0 GB: the proof's process had reached 8.1 GB ... --memory=GB changes the limit`. The budget is CPU time so that a busy machine slows a proof but never fails it: a proof is charged what it would take on 8 idle cores (the CPU time of what runs in sequence, plus, for the queries that run side by side, the longest or all of them divided by 8, whichever is more), which is close to its wall time on an idle machine. That process ends at its budget even if the prover is stuck, and when `smith prove` itself is killed. `-v` first prints a `phase` line for each step (read and link, the gate, flatten, verify) with its time and the module's size in instructions, so a stall shows where it is; the gate counts the functions with a contract of their own, and trusted ones, and when none has a contract or assert of its own it skips flattening and reports nothing to prove. Only what a unit reaches is flattened. Then `-v` lists every function: the proved ones with their wall time, CPU time and charged time, a proof from the cache as `cached: proved before in ...` with the time it took then, and the trusted ones.
- **A proof is kept by its content.** A function proved once is proved again at once while the IR the prover checks for it (its body, the contracts of its callees, the globals and declarations it uses) is the same: the key is a hash of that IR without line numbers, LLVM's version and a version number of the prover (bumped when a change can alter a proof; never the build time, so rebuilding smith keeps the cache), kept under `$XDG_CACHE_HOME/smith/proofs` (else `~/.cache/smith/proofs`), an entry unused for 31 days removed (checked at most once a week). Only a proof that succeeded is kept. `--no-cache` proves again. The summary says `N proved, M failed, K trusted, J cached`.

Reading the cost report: `smith-prove-cost`. See `smith-prove`.
