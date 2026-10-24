---
type: design
title: CI runs the full check on the owner's Mac
description: Every pull request runs smith check on a self-hosted runner under the machine's lock, and a green push to main installs smith.
tags: [ci, process]
---
# CI runs the full check on the owner's Mac

`.github/workflows/check.yml` runs on every pull request to `main` and every push to `main`, on the owner's self-hosted runner (`self-hosted, macOS, ARM64`), which runs as the owner's user: it shares `~/.cache/smith/llvm` and the installed smith. The service reads no shell profile, so the job puts `~/.local/bin` on `PATH` itself.

**The job.** It builds smith from the checked-out commit with the installed smith (`smith build`), then runs `lockf /private/tmp/smith-full-check.lock build/Darwin-arm64/smith check`, so it never overlaps another full check. The full output is the `check-log` artifact; the last lines are the job summary. A new push to a pull request cancels its older run; runs on `main` never cancel each other.

**LLVM is never built here.** If `~/.cache/smith/llvm/<name>` is missing for the pinned name, the job fails and says the lead must run `smith build llvm`.

**On `main`,** after a green check the `install` job runs `smith build install`, so the installed smith is the merged commit (`smith build release` joins when it exists).

**The process.** An implementer pushes its branch, opens a pull request with `gh pr create` linking the issue, and runs only the tests for what it changed. The lead merges (`gh pr merge --squash`, or `--rebase` to keep commits) only when CI is green on the head commit, so the commit that was checked is the commit that is merged.

**Limits.** Each job has `timeout-minutes: 30`. The DAP test client fails with "the debugger is waiting for permission (Developer Tools)" when neither `smith dap` nor its children use CPU for 60 s (the service user lacks the permission), instead of waiting out the 600 s backstop.
