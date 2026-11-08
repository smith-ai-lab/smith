#!/usr/bin/env bash
# The batch wait of a merge-queue run (#2851, knowledge/ci-hosted.md). A queue group's branch is
# gh-readonly-queue/main/pr-<N>-<sha>, where <sha> is the head of the group ahead of it (or main). So a later entry
# is being built on this one exactly when a branch named pr-*-<HEAD_SHA> exists. While one does, this run waits on
# one runner and builds nothing: with HEADGREEN, the later group's passing run merges this entry with it. When none
# does (at the start, or once the later groups failed or left), it returns and the run builds the full checks.
# It never reports success by itself: returning only lets the full checks start.
# HEAD_SHA is the group's own head (the merge_group event's head_sha, a commit made by the queue), never the
# pull request's head: later groups are named after the queue commit.
#   prints `batch=alone` (no wait), `batch=waited` (waited, now last) or `batch=capped` (waited WAIT_CAP s, builds
#   anyway, so wait plus a full run stays under the ruleset's 120-minute check timeout) on stdout; exits 3 when its own
#   branch is gone (the group merged or was dropped): the caller cancels the run.
# Inputs: REPO_URL, OWN_REF (refs/heads/gh-readonly-queue/...), HEAD_SHA; POLL (s, 30), WAIT_CAP (s, 3600);
# LS_REMOTE (default `git ls-remote`, replaced by the test).
set -u
poll=${POLL:-30}
cap=${WAIT_CAP:-3600}
ls_remote=${LS_REMOTE:-git ls-remote}
waited=0
state=alone
while :; do
  if ! refs=$($ls_remote "$REPO_URL" 'refs/heads/gh-readonly-queue/main/*'); then
    # a failed read is not a later group: build (costs runners, never safety)
    echo "batch-wait: ls-remote failed; running full checks" >&2
    echo "batch=$state"
    exit 0
  fi
  if ! printf '%s\n' "$refs" | awk '{print $2}' | grep -qxF "$OWN_REF"; then
    # two reads 5 s apart: cancelling a live group would leave its `ci` unreported until the queue's timeout
    misses=$((${misses:-0} + 1))
    if [ "$misses" -lt 2 ]; then sleep "${MISS_SLEEP:-5}"; continue; fi
    echo "batch-wait: $OWN_REF is gone (merged or dropped)" >&2
    exit 3
  fi
  misses=0
  later=$(printf '%s\n' "$refs" | awk '{print $2}' | grep -E "/pr-[0-9]+-$HEAD_SHA\$" || true)
  if [ -z "$later" ]; then
    [ "$state" = waited ] && echo "batch-wait: now last; running full checks" >&2
    [ "$state" = alone ] && echo "batch-wait: last entry; running full checks" >&2
    echo "batch=$state"
    exit 0
  fi
  if [ "$waited" -ge "$cap" ]; then
    echo "batch-wait: waited ${waited}s on $later; capped, running full checks" >&2
    echo "batch=capped"
    exit 0
  fi
  # one line per change of state, so a batch reads from the logs
  [ "$later" != "${last_later:-}" ] && echo "batch-wait: waiting on $(echo $later)" >&2
  last_later=$later
  state=waited
  sleep "$poll"
  waited=$((waited + poll))
done
