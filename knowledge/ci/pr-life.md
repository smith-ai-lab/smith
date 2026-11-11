# Where a pull request's hours go

How to re-measure the life of the factory's pull requests, from the ticket's claim to the merge (#3082). Run it weekly. Compare the tables with the ones in #3082's body.

## Re-run
From an empty folder (`mktemp -d`), using `gh api` REST GETs only, 100 per page, with every file cached so a second run fetches only what is new:

```
python3 -I <repo>/knowledge/ci/pr-life/fetch.py 2026-11-02T12:00:00Z 2026-11-11   # oldest PR kept, last day of CI runs
python3 -I <repo>/knowledge/ci/pr-life/life.py     # per day merged: commit, open, green, queued, merged; pushes; thrown out
python3 -I <repo>/knowledge/ci/pr-life/life2.py    # the same from the ticket's `claimed` label; waiting's share
python3 -I <repo>/knowledge/ci/pr-life/more.py     # time lost to the queue's removals and to red checks; size; overlap
python3 -I <repo>/knowledge/ci/pr-life/prfail.py   # red PR checks by first failing job: flaky or fixed by a push
python3 -I <repo>/knowledge/ci/pr-life/qfail2.py   # failed queue runs: the PR's fault, or merged later unchanged
```

`life.py` writes `rows.json` and `life2.py` writes `rows2.json`, so run them in this order. 791 PRs took about 3,000 calls and 3 minutes. The session's limit is 15,000 calls an hour.

## What the numbers mean
- **The start is the ticket's `claimed` label**: the latest one before the PR opened. A PR's commits are dated when the runner commits them, usually just before it opens the PR, so "first commit" does not show when work started.
- **Pushes** are the distinct heads that got a PR check. These are `ci` runs from `pull_request` or `workflow_dispatch` events whose `head_branch` is the PR's branch.
- **Thrown out** counts `removed_from_merge_queue` events beyond the one that comes with the merge.
- **A failed queue run is not the PR's fault** when the PR later merged with no commit newer than that run. A run like that was stacked behind a failing entry, hit a flaky test or hit an infra error.
- **A red PR check is the PR's own fault** when the same head never went green and a new push fixed it.
- **Runner-hours** come from `list_sessions`: a session's time from `created_at` to `updated_at`, for the tags `smith-runner` and `smith-worker`. This is an upper bound, because a session waiting on its PR still counts. The session tools are not REST, so this number is taken by hand.
