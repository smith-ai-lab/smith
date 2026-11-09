# gh_retry <command...>: runs the command (usually `gh api ...`) and, when it fails because the installation's REST
# quota is spent (#2986: `API rate limit exceeded for installation`, 1,000 requests an hour for the whole repository),
# reads GET /rate_limit (which does not count), sleeps until the reset and tries again. A wait is not a failure. Any
# other error is returned as it is, at once. Fail-closed: when the waits add up to API_WAIT_MAX seconds (default 1200)
# the last error is returned. Source this file; it defines the function only.
#   API_WAIT_MAX    the most seconds to wait in all (default 1200)
#   API_WAIT_SLEEP  the sleep command (default `sleep`; the test replaces it)
gh_retry() {
  local out err rc waited=0 max=${API_WAIT_MAX:-1200} s
  err=$(mktemp)
  while :; do
    rc=0
    out=$("$@" 2>"$err") || rc=$?
    if [ "$rc" = 0 ] || ! { printf '%s\n' "$out"; cat "$err"; } | grep -qiE 'rate limit exceeded|secondary rate limit'; then
      cat "$err" >&2
      rm -f "$err"
      [ -z "$out" ] || printf '%s\n' "$out"
      return "$rc"
    fi
    if [ "$waited" -ge "$max" ]; then
      echo "api budget: gave up after waiting ${waited} s for the quota" >&2
      cat "$err" >&2
      rm -f "$err"
      return "$rc"
    fi
    # seconds to the reset; /rate_limit is free. Without an answer (or with quota left: a burst limit) pause 30 s.
    s=$(gh api rate_limit --jq '.resources.core | if .remaining > 0 then 30 else (.reset - now | ceil) end' 2>/dev/null) || s=
    case $s in '' | *[!0-9]*) s=30 ;; esac
    s=$((s + 1))
    [ "$s" -le $((max - waited)) ] || s=$((max - waited))
    echo "api budget: waiting ${s} s for the quota" >&2
    ${API_WAIT_SLEEP:-sleep} "$s"
    waited=$((waited + s))
  done
}
