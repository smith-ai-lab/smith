---
type: design
title: CI runs the full check on the owner's Mac
description: Every pull request runs smith check on a self-hosted runner (no lock, one clone per job), and a green push to main installs and releases smith.
tags: [ci, process]
---
# CI runs the full check on the owner's Mac

`.github/workflows/check.yml` runs on every pull request to `main` and every push to `main`, on the owner's self-hosted runner (`self-hosted, macOS, ARM64`), which runs as the owner's user: it shares `~/.cache/smith/llvm` and the installed smith. The service reads no shell profile, so the job puts `~/.local/bin` on `PATH` itself.

**The job.** It builds smith from the checked-out commit with the installed smith (`smith build`), then runs `build/Darwin-arm64/smith check` in its own clone, with no shared lock: the agents' local checks hold `/private/tmp/smith-full-check.lock`, and CI queuing behind them timed out idle. The full output is streamed to the job log and to the `check-log` artifact (with `.smith/log` and a process list taken at the end, so a timeout names the stuck process); the last lines are the job summary, and `SMITH_ACCOUNT=all` puts the per-task table (wall, cpu, processes) in `smith-log/test.log` of the artifact. A new push to a pull request cancels its older run; runs on `main` never cancel each other. When the check fails, the job ends with "what failed": every `FAILED:` task and `FAIL` case from the output and `.smith/log` (also at the top of the job summary), so a long log cut short still names the cause. **A rerun of a pull request's run tests the merge commit of the original run**, not the current `main`; to test against `main` as it is now, push, rebase, or close and reopen the pull request.

**LLVM is never built here.** If `~/.cache/smith/llvm/<name>` is missing for the pinned name, the job fails and says the lead must run `smith build llvm`.

**On `main`,** after a green check the `install` job runs `smith build install`, so the installed smith is the merged commit; the `release` job then publishes it ([release](release.md)). The install job runs `smith build install -DNOTESTS` (the check job already ran the suite on this tree; a local `smith build install` still runs the tests), and `release` runs none. With two runners two main pushes can run at once, so `install` and `release` share one concurrency group (`main-install`, a newer pending run replaces an older pending one) and each skips itself unless its commit is still the head of `main`, so an older commit never installs or becomes the latest release after a newer one.

**A tree is checked once.** A green Full check (a pull request's, or a push's that ran it) uploads a tiny artifact `tree-<tree sha>` (7 days), and a push to `main` whose `git rev-parse HEAD^{tree}` has a live one skips "Debugger permission", "Build smith" and "Full check" (the job stays green, so `install` and `release` go on, and a notice names the run); a pull request never skips, and a tree main moved under (or an expired artifact) runs the full check; `scripts/ci-tree.sh` holds the rule, `tests/ci_tree.sh` tests it. The lookup fails safe: any error, empty or malformed answer from `gh` means "not checked" and the full check runs; only a single run id from a live artifact skips.

**The process.** An implementer pushes its branch, opens a pull request with `gh pr create` linking the issue, and runs only the tests for what it changed. The lead merges (`gh pr merge --squash`, or `--rebase` to keep commits) only when CI is green on the head commit, so the commit that was checked is the commit that is merged.

**Limits.** Each job has `timeout-minutes: 30`. The DAP test client fails with "the debugger is waiting for permission (Developer Tools)" when neither `smith dap` nor its children use CPU for 60 s (the service user lacks the permission), instead of waiting out the 600 s backstop.

**Adding a runner.** CI can run on several runners on this machine (two today: `~/actions-runner` and `~/actions-runner2`), all as the owner's user. GitHub's `svc.sh install` writes the LaunchAgent with `SessionCreate = true`, which puts the runner in its own security session where debugserver refuses to attach ("non-interactive debug session"). After installing, set it false (`plutil -replace SessionCreate -bool false <plist>`) and restart the agent (`launchctl bootout`, wait, `bootstrap`); the user must also be in the `_developer` group (once per user). The "Debugger permission" step of the workflow fails in seconds when a runner cannot debug. Setting these up is the owner's job, not an agent's.
