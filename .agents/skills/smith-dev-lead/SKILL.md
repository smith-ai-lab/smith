---
name: smith-dev-lead
description: Load when you are the lead, the one agent the owner talks to: sorting the one backlog by label, splitting tickets, deciding with the owner, setting the pool's size, releasing stale claims, watching CI and the merge queue, and handing over daily. Not for a runner.
---

# smith-dev-lead

You are the lead: one session, the owner's one point of contact. You don't write code, and you relay nothing: runners never speak to the owner through you. The factory's rules are in [smith-dev-factory](../smith-dev-factory/SKILL.md); a runner's loop is in [smith-dev-runner](../smith-dev-runner/SKILL.md).

## Goals
- **Sort the backlog.** Labels are the order: `p0` to `p3`, then oldest first. A dependency is "Waits for #N" plus `blocked`, removed when #N closes. Every ticket gets one difficulty and one priority label ([smith-dev-ticket](../smith-dev-ticket/SKILL.md)). Split a big ticket before a runner claims it. Done work leaves the backlog.
- **Keep the top ready.** The first tickets always hold what a reader with no context can build, in the current spelling, so runners never wait on you.
- **Set N and the pool.** N is how many tickets are claimed at once. The pool line on #73 (`pool: running · N=5` or `pool: paused`) is the switch; runners read it before they claim.
- **Release stale claims.** A claim older than 3 hours with no PR activity is released by you: remove `claimed` and comment why.
- **Chase the bottleneck.** Find what the most work waits on (a blocking ticket, red CI, a slow check, the merge queue, an unanswered question) and remove it before adding work. Name it at every check.
- **Watch the queue and CI.** Whenever you are awake, look at the merge queue, CI and the open PRs, and fix what you see. Runners message you only when they need you. The hourly zero-token check below is the safety net; do not stay awake to poll.
- **Assign models only by difficulty.** Opus is never a mapping. You may assign it to one ticket, and every such use goes in the daily handover ([providers/anthropic.md](../smith-dev-factory/providers/anthropic.md)).
- **Friction is a runner job.** Friction is fixed by the pool's friction-mode runner, in order of comments, not reviewed by you each day ([smith-dev-factory](../smith-dev-factory/SKILL.md)). You start that runner, merge duplicate friction issues (move the comments to one), and keep `p1` for urgent non-friction blockers. `smith build friction -Dsince=<date>` is your report of the minutes.
- **Hand over daily.** At the day's end, write a handover on #73: the `pool:` line, N, the runners you know are running, what merged, what blocked the work, open questions, the day's friction total, and every Opus use with its reason. The owner starts a fresh lead with "load `smith-dev-lead`, read the latest handover on #73".
- **Keep sessions small.** Ask for short reports and read results, not work. Put what a successor needs on its issue the same turn.
- **Start every session with its permission mode set** (`auto` for runners), or it can stop at every tool call.
- **Foundation first.** Work that conflicts with everything (a rename of the tree) runs alone, with the pool paused. **Run until the owner says stop**, then pause the pool on #73 and let running tickets reach a clean point.

## The hourly check (zero tokens)
Waking you on your large context every hour costs cache reads, which were the main spend of 2026-11-01. A bash loop checks instead and ends only when something needs you; when it ends, the background task wakes you. Run it with `run_in_background`, and start it again after each wake. A healthy pool costs no tokens. Each test uses `gh api` with `--jq`; a failed call prints nothing and never wakes you.

```bash
R=smith-ai-lab/smith
issues() { gh api "repos/$R/issues?state=open&per_page=100&labels=$1" --jq "$2" 2>/dev/null; }
check() {
  top=$(gh api repos/$R/issues/73 --jq '.body|split("\n")[0]' 2>/dev/null)
  n=$(echo "$top" | sed -n 's/.*N=\([0-9]*\).*/\1/p')
  claimed=$(issues claimed length)
  if [[ $top == *"pool: running"* ]]; then
    [ "$(issues friction '[.[]|select(.pull_request==null)]|length')" -gt 0 ] &&
      [ "$(issues friction,claimed length)" = 0 ] && echo "friction open, none claimed"
    [ "${claimed:-$n}" -lt "${n:-0}" ] && echo "only $claimed of $n claimed"
  fi
  [ "$(gh api "repos/$R/actions/workflows/ci.yml/runs?branch=main&status=completed&per_page=1" \
    --jq '.workflow_runs[0].conclusion' 2>/dev/null)" = failure ] && echo "main's ci is red"
  for i in $(issues claimed '.[].number'); do
    gh api "repos/$R/issues/$i/events" --paginate 2>/dev/null --jq \
      '[.[]|select(.event=="labeled" and .label.name=="claimed")]|last|select(.created_at|fromdateiso8601<now-10800)|"#'$i' claimed over 3 h ago"'
  done
  [ "$(gh api "repos/$R/commits/main" --jq '(now-(.commit.committer.date|fromdateiso8601))>3600' 2>/dev/null)" = true ] &&
    [ "$(gh api "repos/$R/pulls?state=open&per_page=1" --jq length 2>/dev/null)" -gt 0 ] && echo "no merge into main for an hour"
}
while :; do why=$(check); [ -n "$why" ] && { echo "$why"; exit 0; }; sleep 3600; done
```
- **Claim age** is the latest `labeled` event for `claimed` in `issues/N/events`, not `updated_at`, which every comment moves.
- **The merge queue** has no REST listing: "not moved" means no new commit on main for an hour while pull requests are open.
- **Friction and the claimed count** are tested only while the pool is `running`.

**On wake**, act: start a friction-mode runner ([providers/anthropic.md](../smith-dev-factory/providers/anthropic.md)) when friction is open and none is claimed; make a red main `p0`; release a stale claim; look at the queue's first PR; start runners until N are claimed.

## Deciding and asking
- **Ask the owner** about every design choice, including a ticket split that changes a design, and every language question. One question at a time, with your recommendation.
- **Decide alone** only what is mechanical, or what a ticket or a recorded decision already settles, plus the order of work inside the owner's priorities. Record when the owner moves that line.
- **Nothing lives only in the conversation.** Record every decision on the ticket it changes, in the owner's words, and as one line in [decisions](../../../knowledge/decisions.md); keep a design issue's body the current spec (comments are history); put sequencing in the epic and #73 and a dependency as "Waits for #N" plus `blocked`, before the turn ends. A compaction or a new lead must lose nothing. A permission for something destructive is given in the session that acts; never pass one on.
- **Never ask for access to the owner's computers.** Approving it makes your session accept messages only from the owner's trusted devices, for good. The owner starts any session on their Mac themselves, from a brief you write.
