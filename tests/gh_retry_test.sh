#!/usr/bin/env bash
# Feeds .github/actions/api-budget/gh-retry.sh the rate-limit error text (#2986): the function must wait and retry,
# not fail; any other error returns at once; a quota that never comes back fails after API_WAIT_MAX.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/.github/actions/api-budget/gh-retry.sh"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }

# a fake gh: `gh api rate_limit` answers the reset; the command under test fails N times with the quota error
gh() { if [ "$2" = rate_limit ]; then echo 7; else echo "unexpected gh $*" >&2; return 9; fi; }
export -f gh 2>/dev/null
flaky() { local n; n=$(cat "$work/n" 2>/dev/null || echo 0); echo $((n + 1)) >"$work/n"
  if [ "$n" -lt "$FAILS" ]; then echo "gh: API rate limit exceeded for installation ID 1234. (HTTP 403)" >&2; return 1; fi
  echo '{"ok":true}'; }
sleeps="$work/sleeps"; : >"$sleeps"
API_WAIT_SLEEP=fake_sleep; fake_sleep() { echo "$1" >>"$sleeps"; }

FAILS=2; echo 0 >"$work/n"
out=$(gh_retry flaky 2>"$work/err"); rc=$?
check "retries through two quota errors" "$rc $out" '0 {"ok":true}'
check "waits until the reset (7 s + 1) twice" "$(tr '\n' ' ' <"$sleeps")" '8 8 '
check "logs the wait" "$(grep -c 'api budget: waiting 8 s for the quota' "$work/err")" 2

other() { echo "gh: Not Found (HTTP 404)" >&2; return 1; }
: >"$sleeps"; out=$(gh_retry other 2>"$work/err"); rc=$?
check "another error returns at once" "$rc $(wc -c <"$sleeps" | tr -d ' ')" '1 0'
check "keeps its message" "$(cat "$work/err")" 'gh: Not Found (HTTP 404)'

FAILS=99; echo 0 >"$work/n"; : >"$sleeps"
API_WAIT_MAX=20 gh_retry flaky 2>"$work/err" >/dev/null; rc=$?
check "gives up when the quota never returns" "$rc" 1
check "says so" "$(grep -c 'gave up after waiting 20 s' "$work/err")" 1
exit $fail
