---
name: watch
description: The lead's one background watcher for the smith factory: the mailbox #1406 every minute and the pipeline checks every hour (ready tickets, red main, runner questions, red PRs nobody touches, a stalled queue), at zero tokens.
---

# The lead's watcher

One bash loop, run with `run_in_background` and the longest timeout. It ends only when something needs the lead, and its end wakes the lead. **Start it again after every wake, including when the 2-hour limit stops it:** on 2026-11-01 it lapsed at 23:15 and runners idled all night unseen. `SEEN` remembers what it already reported, so a known red PR wakes the lead once.

```bash
R=smith-ai-lab/smith
issues() { gh api "repos/$R/issues?state=open&per_page=100&labels=$1" --jq "$2" 2>/dev/null; }
lead() { gh api "repos/$R/issues/1406/comments?per_page=100" --jq '[.[]|select(.body|startswith("@lead:"))]|length' 2>/dev/null; }
SEEN=${SEEN:-$HOME/.lead-seen}; touch "$SEEN"
seen() { grep -qx "$1" "$SEEN" && return 1; echo "$1" >> "$SEEN"; }
check() {
  top=$(gh api repos/$R/issues/73 --jq '.body|split("\n")[0]' 2>/dev/null)
  n=$(echo "$top" | sed -n 's/.*N=\([0-9]*\).*/\1/p')
  if [[ $top == *"pool: running"* ]]; then
    ready=$(issues task '[.[]|select(.pull_request==null and ([.labels[].name]|(index("claimed") or index("blocked"))|not))]|length')
    [ -n "$ready" ] && [ "$ready" -lt "${n:-0}" ] && echo "only $ready ready tickets for N=$n: runners will idle"
  fi
  [ "$(gh api "repos/$R/actions/workflows/ci.yml/runs?branch=main&status=completed&per_page=1" \
    --jq '.workflow_runs[0].conclusion' 2>/dev/null)" = failure ] && echo "main's ci is red"
  for i in $(issues blocked '.[].number'); do
    gh api "repos/$R/issues/$i/events" --paginate 2>/dev/null --jq \
      '[.[]|select(.event=="labeled" and .label.name=="blocked")]|last|select(.created_at|fromdateiso8601>now-3600)|"x"' | grep -q x &&
      gh api "repos/$R/issues/$i/comments" --jq '.[-1].body' 2>/dev/null | grep -qi runner && seen "q$i" && echo "#$i blocked by a runner: a question for you"
  done
  for i in $(issues blocked '.[]|select(.pull_request==null)|.number'); do
    w=$( { gh api "repos/$R/issues/$i" --jq .body; gh api "repos/$R/issues/$i/comments" --jq '.[].body'; } 2>/dev/null | grep -oiE 'waits for[^.]*' | grep -oE '#[0-9]+' | tr -d '#' | sort -u)
    [ -z "$w" ] && continue
    open=0; for d in $w; do [ "$(gh api repos/$R/issues/$d --jq .state 2>/dev/null)" = closed ] || open=1; done
    [ $open = 0 ] && seen "u$i" && echo "#$i: everything it waits for is closed; unblock it"
  done
  for p in $(gh api "repos/$R/pulls?state=open&per_page=50" --jq '.[]|select(.updated_at|fromdateiso8601<now-3600)|.number' 2>/dev/null); do
    sha=$(gh api repos/$R/pulls/$p --jq .head.sha 2>/dev/null)
    [ "$(gh api repos/$R/commits/$sha/check-runs --jq '[.check_runs[]|select(.name=="ci")|.conclusion]|first' 2>/dev/null)" = failure ] && seen "pr$p" && echo "PR #$p red and untouched for an hour"
  done
  for p in $(gh api "repos/$R/pulls?state=open&per_page=50" --jq '.[]|select(.auto_merge==null and (.updated_at|fromdateiso8601<now-1800))|.number' 2>/dev/null); do
    q=$(gh api "repos/$R/issues/$p/timeline?per_page=100" --jq '[.[]|select(.event=="added_to_merge_queue" or .event=="removed_from_merge_queue")|.event]|last // "none"' 2>/dev/null)
    st=$(gh api repos/$R/pulls/$p --jq .mergeable_state 2>/dev/null)
    [ "$q" != added_to_merge_queue ] && [ "$st" = clean ] && seen "q$p-$q" && echo "PR #$p is green but not in the merge queue ($q)"
  done
  [ "$(gh api "repos/$R/commits/main" --jq '(now-(.commit.committer.date|fromdateiso8601))>3600' 2>/dev/null)" = true ] &&
    [ "$(gh api "repos/$R/pulls?state=open&per_page=1" --jq length 2>/dev/null)" -gt 0 ] && echo "no merge into main for an hour"
}
base=$(lead); t=0
while :; do
  m=$(lead); [ -n "$m" ] && [ "$m" -gt "${base:-0}" ] && { echo "new @lead: on #1406"; gh api "repos/$R/issues/1406/comments?per_page=100" --jq '[.[]|select(.body|startswith("@lead:"))][-1].body' | head -40; exit 0; }
  if [ $((t % 60)) = 0 ]; then why=$(check); [ -n "$why" ] && { echo "$why"; exit 0; }; fi
  t=$((t+1)); sleep 60
done
```
- **Ready tickets:** open `task` issues that are neither `claimed` nor `blocked`. Fewer than N means runners exit with nothing to take: unblock, close tickets already done, split a large one, or answer a question.
- **A runner's question:** a ticket a runner marked `blocked` in the last hour. Runners should also post `@lead:` on #1406, which wakes the lead within a minute.
- **Everything a blocked ticket waits for is closed:** remove `blocked` (the "Waits for #N" lines in its body and comments).
- **A green PR not in the merge queue** (never added, or `removed_from_merge_queue` last in its timeline): GitHub ignores auto-merge on a PR that is already green, and the queue drops a PR whose queue run fails. Merge main into it and turn auto-merge on while CI runs (#1522 sat green for 90 minutes).
- **A red PR untouched for an hour:** its runner has ended. Release its ticket with the failing test named, so the next runner fixes that PR (smith-dev-runner step 1).
- **Claim age** is the latest `labeled` event for `claimed` in `issues/N/events`, not `updated_at`, which every comment moves. The merge queue has no REST listing: "not moved" means no new commit on main for an hour while pull requests are open.
