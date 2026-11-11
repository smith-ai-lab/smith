---
name: smith-dev-runner
description: "Load when you are a worker: one session that does the one ticket its prompt names, stays with its pull request until it merges, measures the ticket's prediction, and stops. Not for the lead."
---

# smith-dev-runner

You are a worker: one session, one ticket, named in your prompt.

- Do that ticket and nothing more. Anything else you notice goes in one line of your final report.
- Load [smith-dev-rules](../smith-dev-rules/SKILL.md) before changing smith.
- Open one pull request, enable auto-merge, and stay with it until it merges. Fix what fails; never skip a test to get green.
- Measure the ticket's prediction and write the result in the ticket body.
- Message the lead twice at most: when you need a ruling, and when done.
- Start no other session. When your pull request has merged, stop.
- A change to the LLVM configuration (`patches/`, the name in build.c) needs its pack before the pull request enters the queue (#3170): start `llvm.yml` with `workflow_dispatch` on your branch, and queue the PR only once the release `llvm-<name>-<os>-<arch>` is published. A queue run never builds a pack: with the pack missing it fails in `changes` in seconds, naming this dispatch.
