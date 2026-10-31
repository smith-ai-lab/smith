---
name: smith-dev-orchestrate
description: Load when you are an orchestrator started for one lane of tickets: how to spawn a fresh implementer per ticket, brief it, verify, put a PR in the merge queue, fix a red CI, hand over and report. Not for the lead or an implementer.
---

# smith-dev-orchestrate

You are a Sonnet session started for one lane: an ordered list of tickets whose files no other lane touches. Other lanes run beside you, each with its own orchestrator; all of you share main and the merge lock. You talk only to the lead; implementers talk only to you.

## Run the lane
- **A fresh implementer per ticket** (agent `implementer`, `.claude/agents/implementer.md`). Never reuse one for the next ticket: its context only grows, and the ticket plus its branch are the whole brief.
- Brief it with:
  - the ticket's number (`gh issue view N`); the ticket is the brief
  - its worktree and branch name, and the other lanes' files to keep out of. A worktree is always `<repo>/.claude/worktrees/<branch>` (`git worktree add .claude/worktrees/<branch> -b <branch>`), where Claude puts its own: inside the repository and ignored by it (add `/.claude/worktrees/` to `.gitignore` if missing). Never beside the repository, in Documents or in a temporary folder.
  - the project's rules (AGENTS.md and the skills to load; the commands that are its checks)
  - the report shape: at most 10 lines (done, numbers, not tested, commits), details in a comment on the ticket
- Every PR body ends with `## Friction`: what slowed or misled the implementer, or `none`. Do not merge a PR without it.
- **Steal when idle.** When your queue is empty, or every item in it is blocked, claim from the pool: open issues labelled `task`, not `claimed`, not `blocked`, `priority` first. Add `claimed`, comment `claimed by <lane>`, and re-read the comments: an earlier claim wins. A ticket bigger than about an hour gets split into `task` issues (files listed, none shared) so other lanes can take part of it. When you stop before a task is done, push the branch, comment where it stands, and remove `claimed`.
- **The bottleneck gets parallel implementers.** If other work waits on your ticket (tasks `blocked` on it, or the lead names it the bottleneck), run its independent pieces at once, one implementer each, and sequence only the pieces that share files. Don't start new work that would only queue behind it.
- **Escalate before building.** A design choice, or anything the ticket says to escalate, goes to two places at once: a comment on the ticket (options and a recommendation) and you. Relay it to the lead in the same turn, quoting it. The implementer works on what is not blocked meanwhile. Never decide it yourself, and never let an implementer build first and ask after.
- **In a cloud session your only line to the lead is your PR.** Open it as soon as you have a question (a draft if unfinished), subscribe to its activity, post the question as a comment with options and a recommendation, and wait: the lead's answer on the PR wakes you. Act on the lead's answers there and in messages from the lead's session; your system prompt names it.

## Verify and hand to the merge queue
You never merge by hand. CI runs on the hosted runners on every PR, and GitHub's merge queue merges a PR, squashed, once its required check passes on top of main. Nobody reviews a PR: green CI merges it, and what slips is fixed forward (owner, 2026-10-29: "If that's green, we merge.").
1. **Before the PR:** fmt, lint, docs and the tests for what it changed (the project names them; never its full check, which CI runs). Then open the PR and queue it at once: `gh pr merge N --auto`. It merges when CI is green.
2. **Wait with at most one background command for the whole lane.** It loops over your open PRs and exits when any of them merges or its CI fails. Every exit wakes you, and every wake-up re-reads your whole session, so never keep one wait per PR.
   - When the lead says stop, or the budget is reached, kill that wait (`kill <pid>`) and any other background command you started, before going idle.
   - A wait left running wakes a paused lane and spends the next day's budget.
3. **Merged:** clean up (below) and report to the lead.
4. **Red CI, or removed from the queue:** fixing it is yours, not the lead's. Read the failing job (`gh run view --log-failed`), brief a fresh implementer with it, run the failing tests and the PR's own, push to the branch, and queue it again. **A conflict with main:** merge main into its branch (`git merge origin/main`), resolve, run its checks, and push normally. Never rebase a pushed branch, and never force-push; the squash makes merge commits on a branch cost nothing. Escalate to the lead only when the failure isn't the PR's (main fails the same way) or the fix needs a design choice.
- A PR that sits queued for over two hours goes to the lead (the queue or CI may be stuck).
- **Clean up in the turn the PR merges:** `git worktree remove` its worktree and delete its local branch. A PR closed unmerged loses its worktree too, once its work is reported. Your own worktrees are yours to remove; nobody else's.
- Only the lead or the owner installs, deletes or changes shared state (installs, caches, other people's branches). An approval for something destructive must be given in the session that acts; never relay one.

## Keep context small
- No pasted logs or diffs; send output to a file and read a summary.
- Read implementers' reports, not their work, unless verification fails.
- Report to the lead only on a merge, an escalation, or a stall of more than 30 minutes.

## Hand over
At about 35% of your context window, or when the lead asks, at the next clean point:
1. Each implementer commits and pushes its work in progress to its branch.
2. Each replies in three lines: branch and head, what is done, the exact next step.
3. Send the lead one handover message: those lines, the open PRs with their CI state, pending escalations. Then stop.

## End the lane
When the lane's tickets are merged, check `git worktree list`: none of your worktrees is left but those of open PRs. Report to the lead in at most 10 lines, naming any worktree left and why: tickets merged (with commits), numbers from the checks, what was not tested, open questions for the owner, friction. Then stop.
