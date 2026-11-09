#!/usr/bin/env bash
# Reports the hit rate of the caches a job used (#2876): one line per cache in the job summary, the first misses with
# their reason, and a warning (never a failure) when a queue run whose change touches none of the cache's inputs hits
# less than half. Usage: ci/cache-report.sh <name> <log> [<input-path-regex>]
#   test-store: the log holds `cache test-store: <hits> of <total> (<p>%)` lines (one per runner, `smith test` prints
#               them) and up to 5 `cache test-store: miss <test>: <reason>` lines per runner.
#   steps:      the log is `smith build` output: `[n/m] ...` lines, `(cached)` at the end of a served step.
# The input regex names the files whose change makes a low rate normal; CACHE_BASE is the commit the run is compared
# with (the queue's base). A cache never fails a run: a miss costs time, never correctness.
set -u
name=$1
log=$2
inputs=${3:-'^$'}
summary=${GITHUB_STEP_SUMMARY:-/dev/null}
hits=0 total=0
if [ "$name" = steps ] || [ "$name" = tests-built ]; then
  total=$(grep -cE '^\[[0-9]+/[0-9]+\] ' "$log" 2>/dev/null || true)
  hits=$(grep -c ' (cached)$' "$log" 2>/dev/null || true)
else
  while read -r h t; do hits=$((hits + h)); total=$((total + t)); done < <(
    sed -n "s/^cache $name: \([0-9]*\) of \([0-9]*\) (.*/\1 \2/p" "$log" 2>/dev/null)
  grep "^cache $name: miss " "$log" 2>/dev/null | head -n 5 | tee -a "$summary"
fi
if [ "${total:-0}" -eq 0 ]; then
  echo "cache $name: 0 of 0 (no count)" | tee -a "$summary"
  exit 0
fi
pct=$((hits * 100 / total))
echo "cache $name: $hits of $total ($pct%)" | tee -a "$summary"
if [ "$pct" -lt 50 ] && [ "${GITHUB_EVENT_NAME:-}" = merge_group ]; then
  touched=$(git diff --name-only "${CACHE_BASE:-HEAD^}" HEAD 2>/dev/null | grep -cE "$inputs" || true)
  if [ "${touched:-0}" -eq 0 ]; then
    echo "::warning::cache $name hit $pct% ($hits of $total) though this change touches none of its inputs; the miss lines in the job summary say which input differs"
  fi
fi
exit 0
